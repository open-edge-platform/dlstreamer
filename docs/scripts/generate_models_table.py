# ==============================================================================
# Copyright (C) 2022-2025 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import os
import re
import yaml
import string
import json
from argparse import ArgumentParser
from jsonschema import validate

DLSTREAMER_URL='https://github.com/open-edge-platform/dlstreamer/tree/master/'
PIPELINE_ZOO_URL='https://github.com/dlstreamer/pipeline-zoo-models/tree/main/'

dldt_str = 'dl' + 'dt'
openvino_str = 'open' + 'vino'
dlstreamer_name = 'Deep ' + 'Learning' + ' Streamer'

parser = ArgumentParser(add_help=False)
_args = parser.add_argument_group('Options')
_args.add_argument("-omz", "--open_model_zoo", help="Required. Path to Open Model Zoo cloned repo", required=True, type=str)
_args.add_argument("-mi", "--model_index", help="Required. Path to model_index.yaml file", required=True, type=str)
_args.add_argument("-o", "--output", help="Required. Path to output .rst file", required=True, type=str)
_args.add_argument("-a", "--all", help="If true, table will contain all models, not only supported", required=False, type=str)
args = parser.parse_args()

models={}

# read .yml and .md files from open_model_zoo
for root, dirs, files in os.walk(args.open_model_zoo + '/models', topdown=False):
    name = os.path.basename(root)
    if 'composite-model.yml' in files:
        with open(os.path.join(root, 'composite-model.yml')) as f:
            models[name] = yaml.safe_load(f)
    if 'model.yml' in files and not os.path.exists(os.path.join(root, '../composite-model.yml')):
        with open(os.path.join(root, 'model.yml')) as f:
            models[name] = yaml.safe_load(f)
    if 'README.md' in files:
        if name not in models:
            print("WARNING: README.md without model.yml:", name)
            models[name] = {}
        with open(os.path.join(root, 'README.md')) as f:
            for line in f.readlines():
                vec = [x.strip() for x in line.split('|')]
                if len(vec) < 3 or vec[1].startswith('---'):
                    continue
                #print(vec)
                models[name][vec[1]] = vec[2]
    if name in models:
        if '/intel/' in root:
            models[name]['source'] = openvino_str
        elif '/public/' in root:
            models[name]['source'] = 'public'
        format = models[name].get('framework', '').replace(dldt_str, openvino_str)
        if openvino_str not in format:
            format += ', ' + openvino_str
        models[name]['format'] = format
        models[name]['readme'] = 'https://docs.openvino.ai/latest/omz_models_model_' + name.replace('-', '_').replace('.', '_') + '.html'

# for each model, find if OV supports CPU, GPU devices
for md in ['models/intel/device_support.md', 'models/public/device_support.md']:
    with open(os.path.join(args.open_model_zoo, md)) as f:
        for line in f.readlines():
            vec = [x.strip() for x in line.split('|')]
            if len(vec) < 4:
                continue
            name = vec[1]
            for composite_name, m in models.items():
                if name in m.get('stages_order', []):
                    name = composite_name
                    break
            if name in models:
                openvino_devices = ''
                if vec[2] == 'YES':
                    openvino_devices += 'CPU'
                if vec[3] == 'YES':
                    openvino_devices += ', GPU'
                models[name]['openvino_devices'] = openvino_devices

# for each model, find Accuracy, GFlops, mParams
for md in ['models/intel/index.md', 'models/public/index.md']:
    with open(os.path.join(args.open_model_zoo, md)) as f:
        for line in f.readlines():
            for name in list(models.keys()):
                if '[' + name + ']' in line or name + '-encoder' in line:
                    vec = [x.strip() for x in line.split('|')]
                    if len(vec) < 5:
                        continue
                    #print(vec)
                    if '%' in vec[-4]:
                        models[name]['Accuracy'] = vec[-4]
                    models[name]['GFlops'] = vec[-3]
                    models[name]['mParams'] = vec[-2]

# read accuracy_checker parameters
accuracy_checker = os.path.join(args.open_model_zoo, 'tools/accuracy_checker/configs')
for file in os.listdir(accuracy_checker):
    if not file.endswith(".yml"):
        continue
    with open(os.path.join(accuracy_checker, file)) as f:
        yml = yaml.safe_load(f)
        if 'models' in yml:
            models_list = yml['models']
        else:
            models_list = yml.get('evaluations', [])
        for m in models_list:
            name = m['name']
            filename = os.path.splitext(file)[0]
            if name in models:
                models[name].update(m)
            elif filename in models:
                models[filename].update(m)
            else:
                print('WARNING: model listed in OV accuracy_checker but not listed in OV model zoo:', name)

# for each model, find which OMZ demo supports it
for root, dirs, files in os.walk(args.open_model_zoo, topdown=False):
    if 'models.lst' in files:
        with open(os.path.join(root, 'models.lst')) as f:
            omz_demo = os.path.basename(os.path.dirname(root))
            for line in f.read().splitlines():
                if line[0] == '#':
                    continue
                name_regexp = line.replace('?', '.').replace('-encoder','')
                for composite_name, m in models.items():
                    for name in m.get('stages_order', [composite_name]):
                        if re.match(name_regexp, name):
                            models[composite_name]['omz_demo'] = omz_demo
                            break

# read model_index.yaml
with open(args.model_index) as f:
    model_index_schema = args.model_index.replace(".yaml", "_schema.json")
    with open(model_index_schema) as mis:
        model_index = yaml.safe_load(f)
        validate(model_index, json.load(mis))
        for name, m in model_index.items():
            if 'dlstreamer_support' not in m:
                m['dlstreamer_support'] = openvino_str
            if 'labels-file' in m:
                m['labels-file'] = DLSTREAMER_URL + 'samples/' + m['labels-file']
            if 'model-proc' in m:
                m['model-proc'] = DLSTREAMER_URL + 'samples/' + m['model-proc']
            if m.get('source', '') == 'dlstreamer':
                if 'readme' not in m:
                    m['readme'] = PIPELINE_ZOO_URL + 'storage/' + name
                if 'model-proc' not in m:
                    m['model-proc'] = PIPELINE_ZOO_URL + 'storage/' + name # + '/' + name + '.json'
            if name in models:
                models[name].update(m)
            else:
                if 'source' not in m and name + '-decoder' not in models:
                    print('WARNING: model listed in model_index.yaml but not listed in OV model zoo:', name)
                if 'format' not in m:
                    m['format'] = openvino_str
                models[name] = m

# remove models that we don't want to include, e.g. preview/deprecation
models_to_remove = ['icnet-camvid-ava-0001', 'road-segmentation-adas-0001',
                    'semantic-segmentation-adas-0001', 'text-detection-0003']
for model_name in models_to_remove:
    if models.pop(model_name, None):
        print(f"Removed '{model_name}' from final models list")

# update name, task_type, devices
for name, m in models.items():
    m['name'] = name
    if 'yolo' in name:
        m['task_type'] = 'detection'
    if 'task_type' not in m:
        m['task_type'] = '~?'
    format = m.get('format', '')
    if 'pytorch_devices' not in m and 'pytorch' in format:
        m['pytorch_devices'] = 'CPU'
    if 'tf_devices' not in m and 'tf' in format:
        m['tf_devices'] = 'CPU'

# convert into list sorted by 'task_type'
models = sorted(models.values(), key=lambda x: x['task_type'] + x['name'])

for m in models:
    with open(m['name'] + '_files.yml', 'w') as f:
        yaml.dump(m.get('files', m.get('readme', {})), f, sort_keys=False)
    if 'files' in m:
        del m['files']
    with open(m['name'] + '.yml', 'w') as f:
        yaml.dump(m, f, sort_keys=False)

# write .rst
with open(args.output, 'w') as f:
    f.write('Supported Models\n')
    f.write('================\n')
    f.write('\n')
    f.write('This page contains table of pre-trained models with information ')
    f.write('about model support on various inference backends and CPU/GPU devices.\n')
    f.write('\n')
    f.write('Each model has link (under model name) to original documentation with download instructions.\n')
    f.write('\n')
    f.write('Most models are from `OpenVINO™ Open Model Zoo <https://docs.openvino.ai/latest/model_zoo.html>`__\n')
    f.write('but some models are from other sources (see column Source Repo).\n')
    f.write('\n')
    f.write('Abbreviations used in the table\n')
    f.write('----------------\n')
    f.write('.. list-table::\n')
    f.write('    :header-rows: 1\n')
    f.write('\n')
    f.write('    * - Abbreviation\n')
    f.write('      - Description\n')
    f.write('    * - {0}\n'.format(openvino_str))
    f.write('      - `OpenVINO™ toolkit <https://docs.openvino.ai/>`__ - as model file format (*.xml + *.bin) and inference backend in {0}\n'.format(dlstreamer_name))
    f.write('    * - pytorch\n')
    f.write('      - `PyTorch* framework <https://pytorch.org>`__ - as model file format and inference backend in {0}\n'.format(dlstreamer_name))
    f.write('    * - tf\n')
    f.write('      - `TensorFlow* framework <https://www.tensorflow.org>`__ - as model file format and inference backend in {0}\n'.format(dlstreamer_name))
    f.write('    * - onnx\n')
    f.write('      - `ONNX <https://onnx.ai>`__ - Open Neural Network Exchange file format\n')
    f.write('    * - caffe\n')
    f.write('      - `Caffe* framework <https://caffe2.ai/>`__ - as model file format\n')
    f.write('    * - dlstreamer\n')
    f.write('      - {0}\n'.format(dlstreamer_name))
    f.write('\n')
    f.write('Models Table\n')
    f.write('----------------\n')
    f.write('\n')
    f.write('.. list-table::\n')
    f.write('    :header-rows: 1\n')
    f.write('\n')
    f.write('    * - #\n')
    f.write('      - Category\n')
    f.write('      - Model Name\n')
    f.write('      - Source Repo\n')
    f.write('      - Available Format(s)\n')
    f.write('      - GFLOPs\n')
    f.write('      - {0} support\n'.format(dlstreamer_name))
    f.write('      - Open VINO™ support\n')
    f.write('      - Py Torch* support\n')
    f.write('      - Tensor Flow* support\n')
    f.write('      - labels-file\n')
    f.write('      - model-proc\n')
    f.write('      - OpenVINO™ Open Model Zoo demo app\n')
    f.write('\n')
    n = 0
    for m in models:
        if not args.all and 'dlstreamer_support' not in m:
            continue
        name = m['name']
        name_s = name.replace('torchvision.models.detection.', 'torchvision.models.detection. ')
        n = n + 1
        f.write('    * - {0}\n'.format(n))
        f.write('      - {0}\n'.format(string.capwords(m.get('task_type', '').replace('_',' '))))
        f.write('      - `{0} <{1}>`__\n'.format(name_s, m.get('readme', '')))
        f.write('      - {0}\n'.format(m.get('source', '')))
        f.write('      - {0}\n'.format(m.get('format', '')))
        f.write('      - {0}\n'.format(re.split(r"[^0-9\.]", m.get('GFLOPs', m.get('GFlops', ' ')))[0]))
        f.write('      - {0}\n'.format(m.get('dlstreamer_support', '')))
        f.write('      - {0}\n'.format(m.get('openvino_devices', '?')))
        f.write('      - {0}\n'.format(m.get('pytorch_devices', '')))
        f.write('      - {0}\n'.format(m.get('tf_devices', '')))
        labels_file = m.get('labels-file', None)
        if labels_file:
            f.write('      - `{0} <{1}>`__\n'.format(os.path.basename(labels_file), labels_file))
        else:
            #f.write('      - {0}\n'.format(m.get('datasets', [{}])[0].get('name','').replace('-',' ').replace('_',' ')))
            f.write('      -\n')
        model_proc = m.get('model-proc', None)
        if model_proc:
            f.write('      - `{0} <{1}>`__\n'.format("model-proc", model_proc))
        else:
            f.write('      -\n')
        f.write('      - {0}\n'.format(m.get('omz_demo', '')))
    f.write('\n')
    f.write('Legal Information\n')
    f.write('----------------\n')
    f.write('PyTorch, TensorFlow, Caffe, Keras, MXNet are trademarks or brand names of their respective owners.\n')
    f.write('All company, product and service names used in this website are for identification purposes only.\n')
    f.write('Use of these names,trademarks and brands does not imply endorsement.\n')
