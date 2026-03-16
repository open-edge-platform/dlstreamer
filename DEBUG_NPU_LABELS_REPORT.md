# Raport debugowania: NPU zwraca zerowe etykiety (labels) dla modeli detekcji

## 1. Opis problemu

Przy uruchomieniu pipeline GStreamer z `gvadetect device=NPU` na modelu detekcji
(`optimized_model.xml` — model typu ATSS z wyjściami `boxes` i `labels`), tensor
wyjściowy `labels` zawiera wyłącznie zera. Na `device=CPU` te same etykiety
przyjmują poprawne wartości (0, 1, 2).

**Pipeline testowy:**
```bash
gst-launch-1.0 filesrc location=$VIDEO_EXAMPLES_DIR/warehouse.avi ! decodebin3 \
  ! gvadetect device=NPU model-instance-id=instnpu0 inference-region=full-frame \
    inference-interval=1 batch-size=1 nireq=4 \
    model=/home/dlstreamer/dlstreamer/dynamic_batch_models/optimized_model.xml \
    name=detection \
  ! queue ! gvametaconvert add-empty-results=true name=metaconvert \
  ! gvametapublish file-format=2 file-path=$RESULTS_DIR/pdd.json \
  ! queue ! gvawatermark ! gvafpscounter \
  ! vah264enc ! h264parse ! mp4mux \
  ! filesink location=$RESULTS_DIR/pdd.mp4
```

**Objawy:**
| Metryka | CPU | NPU (bug) |
|---------|-----|-----------|
| Detekcje | 3561 | 3545 (z poprawką skip set_batch) / 0 (bez) |
| label_id=0 (defect) | 900 | **3545** (wszystkie!) |
| label_id=1 (box) | 2294 | **0** |
| label_id=2 (shipping_label) | 367 | **0** |

Bounding boxy (confidence, współrzędne) są poprawne — problem dotyczy **wyłącznie** ścieżki obliczeniowej etykiet.

---

## 2. Architektura przepływu danych (inference → post-processing)

```
OpenVINOImageInference::WorkingFunction()
  └─ output_blobs[name] = OpenvinoOutputTensor(infer_request.get_output_tensor(i))
       └─ callback(output_blobs, frames)
            └─ InferenceImpl::InferenceCompletionCallback()
                 └─ PostProcessor::process(blobs, frames)
                      └─ BoxesLabelsScoresConverter::convert(output_blobs)
                           ├─ boxes_blob = output_blobs.at("boxes")
                           ├─ labels_blob = output_blobs.at("labels")  // BoxesLabelsConverter
                           └─ parseOutputBlob(boxes_data, ..., labels_blob, ...)
                                └─ getLabelIdConfidence(labels_blob, i, confidence)
```

### Kluczowe pliki:

| Plik | Rola |
|------|------|
| `src/monolithic/inference_backend/image_inference/openvino/openvino_image_inference.cpp` | Kompilacja modelu, wykonanie inferencji, tworzenie output blobów |
| `src/monolithic/inference_backend/image_inference/openvino/openvino_blob_wrapper.h` | `OpenvinoOutputTensor` — wrapper na `ov::Tensor`, mapuje `ov::element::Type` → `Blob::Precision` |
| `src/monolithic/gst/inference_elements/common/post_processor/converters/to_roi/boxes_labels_scores_base.cpp` | Bazowa klasa parsowania boxes+labels |
| `src/monolithic/gst/inference_elements/common/post_processor/converters/to_roi/boxes_labels.cpp` | `BoxesLabelsConverter` — obsługuje FP32, I32, I64 dla labels |
| `src/monolithic/gst/inference_elements/base/copy_blob_to_gststruct.cpp` | Kopiowanie blobów do GstStructure z uwzględnieniem precyzji |

---

## 3. Analiza topologii modelu

### 3.1. Ścieżka `boxes` (FP32 — działa na NPU)

```
Concat_4 [FP32, {-1, 100, 5}] → Result_7223 ("boxes")
```

Prosta ścieżka — wyłącznie operacje FP32 (dekodowanie bbox, Concat). NPU obsługuje ją poprawnie.

### 3.2. Ścieżka `labels` (I64 + shape-dependent ops — ZEPSUTA na NPU)

```
                    ShapeOf [model_input] → I64 shape [4]
                       ↓
                    Gather → I64 batch_dim
                       ↓
                    Squeeze → I64 scalar
                       ↓
                    Range(0, batch_dim, 1) → I64 [batch]        ← DYNAMIC
                       ↓
                    Reshape → I64 [batch, 1] (batch_inds)
                       ↓
                    Convert(I64→I32) → I32 [batch, 1]
                       ↓
Split ──→ Multiply(batch_inds_i32, num_classes) → I32 [batch, 1]
                       ↓
        TopK_indices(I64→I32) + Add → I32 [batch, 100]    ← flat indices
                       ↓
  argmax_labels(I64) + Gather(I64, I32_indices, axis=0) → I64 [batch, 100, batch]
                       ↓
                    Reshape → I64 [batch, 100] (topk_labels)
                       ↓
                    Convert(I64→FP32) → FP32 [batch, 100] ("labels")
                       ↓
                    Result_7222
```

**Kluczowe obserwacje:**
1. Ścieżka labels zawiera **operacje zależne od kształtu** (`ShapeOf`, `Range`, `ReduceProd`)
2. Operuje głównie na **I64** — typie, który NPU obsługuje z ograniczeniami
3. Zawiera wielokrotne **konwersje I64↔I32** i finalną **Convert(I64→FP32)**
4. Dynamiczny wymiar batch (`-1`) jest kluczowy dla poprawności `ShapeOf` → `Range`

### 3.3. Porównanie ścieżek — dlaczego boxes działa a labels nie

| Aspekt | Boxes | Labels |
|--------|-------|--------|
| Typ danych | Wyłącznie FP32 | Mieszany: I64, I32, FP32 |
| Operacje kształtu | Brak | ShapeOf, Range, Squeeze, Reshape |
| Dynamiczny batch | Proste broadcastowanie | Komplex: Range(0, batch_dim) tworzy sekwencję indeksów |
| Konwersje typów | Brak | I64→I32, I64→FP32 |

---

## 4. Próby naprawy i ich wyniki

### 4.1. Usunięcie węzła Convert(I64→FP32) z wyjścia

**Koncepcja:** Skoro `BoxesLabelsConverter::getLabelIdConfidence()` natywnie obsługuje I64,
usunąć niepotrzebną konwersję z grafu.

**Implementacja:**
```cpp
// W configure_model(), po ov::set_batch:
if (_device.find("NPU") != std::string::npos) {
    for (auto &output : _model->outputs()) {
        auto result_node = output.get_node_shared_ptr();
        auto producer = result_node->input_value(0);
        auto producer_node = producer.get_node_shared_ptr();
        if (auto convert_op = std::dynamic_pointer_cast<ov::op::v0::Convert>(producer_node)) {
            auto src_type = convert_op->input_value(0).get_element_type();
            auto dst_type = convert_op->get_destination_type();
            if ((src_type == ov::element::i64 || src_type == ov::element::i32)
                && dst_type == ov::element::f32) {
                result_node->input(0).replace_source_output(convert_op->input_value(0));
            }
        }
    }
    _model->validate_nodes_and_infer_types();
}
```

**Wynik:** ❌ **CZĘŚCIOWY SUKCES** — detekcje pojawiły się (3545), ale **wszystkie label_id=0**.
Problem leży głębiej niż sam Convert — operacje I64 w NPU (Gather, Reshape) zwracają zera.

**Dodatkowy problem:** Po usunięciu Convert, nazwy tensorów się gubiły. Port wyjściowy z
Reshape (layer 1150) miał nazwy `{545, topk_labels}`, a po podłączeniu do Result, `get_any_name()`
zwracał **"545"** zamiast **"labels"** — co powodowało `std::out_of_range` w `output_blobs.at("labels")`.

**Lekcja:** Przy manipulacji grafem OV, nazwy tensorów są powiązane z portami wyjściowymi węzłów.
Trzeba użyć `output_port.set_names({"labels"})` (nie `add_names`!) aby zachować oczekiwaną nazwę.

### 4.2. Zmiana Convert na I64→I32 zamiast I64→FP32

**Koncepcja:** Może NPU lepiej obsługuje konwersję I64→I32 niż I64→FP32.

**Implementacja:**
```cpp
if (auto convert_op = std::dynamic_pointer_cast<ov::op::v0::Convert>(producer_node)) {
    if (convert_op->get_destination_type() == ov::element::f32) {
        auto new_convert = std::make_shared<ov::op::v0::Convert>(
            convert_op->input_value(0), ov::element::i32);
        result_node->input(0).replace_source_output(new_convert->output(0));
    }
}
```

**Wynik:** ❌ **Te same zera.** Problem nie jest w samej konwersji typów,
lecz w operacjach I64 wcześniej w grafie.

### 4.3. Pominięcie `ov::set_batch` dla NPU z batch_size=1

**Koncepcja:** `ov::set_batch(_model, 1)` konwertuje dynamiczny wymiar batch `-1`
na statyczny `1` w grafie. Operacje `ShapeOf` → `Range` w ścieżce labels mogą nie
działać poprawnie na NPU po tej transformacji.

**Implementacja (aktualnie w kodzie):**
```cpp
if (_batch_size == 1 && _device.find("NPU") != std::string::npos) {
    GVA_INFO("NPU: skipping ov::set_batch for batch_size=1 to preserve dynamic batch");
} else {
    ov::set_batch(_model, _batch_size);
}
```

**Wynik:** ⚠️ **CZĘŚCIOWY SUKCES** — detekcje pojawiły się (3545 vs 3561 na CPU),
bounding boxy poprawne, ale **wszystkie label_id=0**. Identyczny symptom jak 4.1.

---

## 5. Diagnoza głównego problemu

### Problem jest w obsłudze operacji I64 na NPU

Ścieżka labels w modelu intensywnie korzysta z operacji na tensorach I64:
- `Gather(I64_data, I32_indices, axis=0)` — layer 1148
- `Reshape(I64)` — layers 1124, 1142, 1150
- `Range(I32_start, I64_stop, I32_step) → I64` — layer 1140

**Na CPU** wszystkie te operacje zwracają poprawne wartości.
**Na NPU** te operacje zwracają zera (lub są obliczane niepoprawnie).

Ścieżka boxes operuje wyłącznie na **FP32** i działa poprawnie na NPU, co potwierdza,
że NPU prawidłowo obsługuje inferencję — problem jest specyficzny dla operacji I64.

### Potwierdzone dane diagnostyczne

Blob labels po inferencji NPU:
```
labels_scores_blob dims: [1, 100], size=100, precision=10 (FP32)
labels_scores_blob data (100 values): 0 0 0 0 0 0 ... 0 0 0
```

Blob labels po inferencji CPU:
```
labels_scores_blob dims: [1, 100], size=100, precision=10 (FP32)
labels_scores_blob data (100 values): 1 1 1 1 1 2 1 1 1 1 1 0 0 ...
```

### Dlaczego problemu nie widać jako crash

Pozornie pipeline wygląda na działający:
- Bounding boxy są poprawne (FP32 path)
- Labels = 0 to prawidłowa wartość etykiety (klasa "defect")
- Detekcje pojawają się, ale wszystkie mają `label_id=0`

---

## 6. Analiza obsługi precyzji w DL Streamer

### Post-processory obsługujące różne precyzje labels

| Plik | Obsługiwane precyzje |
|------|---------------------|
| `boxes_labels.cpp` (`getLabelIdConfidence`) | FP32, I32, U32, I64, U64 |
| `boxes_scores.cpp` | Wymaga FP32 (throw jeśli inna) |
| `detection_output.cpp` | Wymaga FP32 (throw jeśli inna) |
| `yolo_base.cpp` | Wymaga FP32 (throw jeśli inna) |
| `label.cpp` (to_tensor) | FP32, FP64, I32 |

### Post-processory zakładające FP32 bez weryfikacji (potencjalny bug)

Następujące pliki wykonują `reinterpret_cast<const float*>(blob->GetData())`
**bez sprawdzenia** `blob->GetPrecision()`:

- `boxes_labels_scores_base.cpp` — dla **boxes** (nie labels)
- `yolo_v26.cpp`, `yolo_v7.cpp`, `yolo_v8.cpp`, `yolo_v10.cpp`, `yolo_x.cpp`
- `mask_rcnn.cpp`, `centerface.cpp`

Jeśli NPU zwróci boxy jako FP16 zamiast FP32, te pliki będą interpretować dane niepoprawnie.

### `OpenvinoOutputTensor` — brak konwersji typów

`OpenvinoOutputTensor` w `openvino_blob_wrapper.h` jest lekkim wrapperem na `ov::Tensor`:
- `GetData()` zwraca surowy wskaźnik bez kopii
- `GetPrecision()` raportuje typ as-is z `ov::Tensor::get_element_type()`
- **Nie ma żadnej konwersji typów** — jeśli NPU zwraca FP16, dane trafiają surowe do post-processora

---

## 7. Potencjalne rozwiązania (do zbadania)

### 7.1. Konwersja precyzji modelu przed kompilacją (rekomendowane)

OpenVINO oferuje pass `ov::pass::ConvertPrecision` do konwersji typów w grafie.
Można skonwertować wszytkie I64 na I32 przed kompilacją na NPU:

```cpp
#include <openvino/pass/manager.hpp>
#include <transformations/convert_precision.hpp>

if (_device.find("NPU") != std::string::npos) {
    ov::pass::Manager manager;
    static const precisions_map precisions = {{ov::element::i64, ov::element::i32}};
    manager.register_pass<ov::pass::ConvertPrecision>(precisions);
    manager.run_passes(_model);
}
```

**Ryzyko:** Może zmienić semantykę modelu jeśli wartości nie mieszczą się w I32.
Jednak w tym przypadku etykiety (0, 1, 2) i indeksy (0-3549) bezpiecznie mieszczą się w I32.

**Status:** Nie przetestowane — wymaga `#include <transformations/convert_precision.hpp>`
który może nie być dostępny w publicznym API OpenVINO (plik z `openvino-dev/transformations`).

### 7.2. Modyfikacja modelu na etapie eksportu

Naprawić problem u źródła — wyeksportować model z PyTorch tak, aby ścieżka labels
korzystała z I32 zamiast I64 (np. dodając `.to(torch.int32)` w postprocessingu modelu
przed eksportem do ONNX/OpenVINO IR).

### 7.3. Konwersja wyjść po inferencji (runtime workaround)

W `WorkingFunction()` sprawdzić precyzję output blobów i skonwertować I64→FP32 ręcznie:

```cpp
void WorkingFunction(const std::shared_ptr<BatchRequest> &request) {
    std::map<std::string, OutputBlob::Ptr> output_blobs;
    const auto &outputs = _impl->_compiled_model.outputs();
    for (size_t i = 0; i < outputs.size(); i++) {
        auto name = outputs[i].get_any_name();
        auto tensor = request->infer_request_new.get_output_tensor(i);
        // NPU workaround: konwertuj zerowe I64 labels
        if (tensor.get_element_type() == ov::element::i64 && _device.find("NPU") != ...) {
            // Skopiuj jako FP32
        }
        output_blobs[name] = std::make_shared<OpenvinoOutputTensor>(tensor);
    }
    callback(output_blobs, request->buffers);
}
```

**Wada:** Nie rozwiązuje podstawowego problemu — dane I64 z NPU to zera.

### 7.4. Wymuszenie FP32 output przez PrePostProcessor

```cpp
auto ppp = ov::preprocess::PrePostProcessor(_model);
for (size_t i = 0; i < _model->outputs().size(); i++) {
    if (_model->output(i).get_element_type() == ov::element::i64) {
        ppp.output(i).tensor().set_element_type(ov::element::f32);
    }
}
_model = ppp.build();
```

**Status:** Nie przetestowane. PrePostProcessor dodaje Convert po Result, ale problem
jest w operacjach I64 **wewnątrz** grafu, więc raczej nie pomoże.

---

## 8. Stan aktualny kodu (zmiany w repozytorium)

### Zmienione pliki:

1. **`openvino_image_inference.cpp`**:
   - Dodany `#include <openvino/op/convert.hpp>`
   - Skip `ov::set_batch` dla NPU z `batch_size=1` (częściowy workaround)
   - Debug logging w `WorkingFunction` (do usunięcia)

2. **`boxes_labels_scores_base.cpp`**:
   - Dodany `fprintf` do logowania wyjątków ATSS (pomocne przy diagnozie)
   - Usunięty oryginalny debug code (`static bool hej`)

### Tymczasowy debug code do usunięcia:
- `fprintf(stderr, "ATSS inner exception: %s\n", ...)` w `boxes_labels_scores_base.cpp:151`
- `static bool debug_once` + `fprintf(stderr, "DEBUG WorkingFunction ...")` w `openvino_image_inference.cpp:1577-1583`

---

## 9. Wnioski i rekomendacje

1. **Przyczyna root cause:** NPU plugin OpenVINO nie obsługuje poprawnie operacji
   na tensorach I64 (Gather, Reshape, Range) — zwraca zera. Jest to bug/ograniczenie
   NPU runtime, nie DL Streamera.

2. **Najlepsze rozwiązanie:** Konwersja I64→I32 w całym grafie modelu przed kompilacją
   na NPU (podejście 7.1), lub naprawienie modelu na etapie eksportu (7.2).

3. **DL Streamer powinien:** Dodać walidację/konwersję precyzji output blobów
   aby zabezpieczyć się przed niespodziewanymi typami z różnych urządzeń.

4. **Do dalszego zbadania:**
   - Czy `ov::pass::ConvertPrecision(i64→i32)` jest dostępny i działa na tym modelu
   - Czy bug NPU z I64 jest znany i raportowany w OpenVINO issue tracker
   - Czy inne modele z operacjami I64 (np. modele NLP) mają ten sam problem na NPU
