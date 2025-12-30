# ==============================================================================
# Copyright (C) 2024-2025 Intel Corporation
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

OV_MODEL_ZOO_URL = 'https://github.com/openvinotoolkit/open_model_zoo/tree/master/'
DLSTREAMER_URL='https://github.com/open-edge-platform/dlstreamer/tree/master/'
PIPELINE_ZOO_URL='https://github.com/dlstreamer/pipeline-zoo-models/tree/main/'

dldt_str = 'dl' + 'dt'
openvino_str = 'open' + 'vino'
dlstreamer_name = 'Deep ' + 'Learning' + ' Streamer'

parser = ArgumentParser(add_help=False)
_args = parser.add_argument_group('Options')
_args.add_argument("-omz", "--open_model_zoo", help="Required. Path to Open Model Zoo cloned repo", required=True, type=str)
_args.add_argument("-mi", "--model_index", help="Path to existing model_index.yaml file", required=False, type=str)
_args.add_argument("-o", "--output", help="Required. Path to output model_index.yaml file", required=True, type=str)
_args.add_argument("-a", "--all", help="If true, table will contain all models, not only supported", required=False, type=str)
args = parser.parse_args()

models={}
deffective_models=[]

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
        cnt=0
        with open(os.path.join(root, 'README.md')) as f:
            for line in f.readlines():
                vec = [x.strip() for x in line.split('|')]
                if len(vec) < 3 or vec[1].startswith('---'):
                    continue
                #print(vec)
                cnt+=1
                if cnt < 2:
                    continue
                models[name][vec[1]] = vec[2]
    if name in models:
        if 'models/intel/' in root:
            models[name]['source'] = 'intel' #openvino_str
        elif 'models/public/' in root:
            models[name]['source'] = 'public'
        format = models[name].get('framework', '').replace(dldt_str, openvino_str)
        if openvino_str not in format:
            format += ', ' + openvino_str
        models[name]['format'] = format
        #models[name]['readme'] = 'https://docs.openvino.ai/latest/omz_models_model_' + name.replace('-', '_').replace('.', '_') + '.html'
        models[name]['readme'] = '/'.join((OV_MODEL_ZOO_URL, 'models', models[name]['source'], name))
        print(models[name]['readme'])



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
                deffective_models.append(name)

# for each model, find which OMZ demo supports it
for root, dirs, files in os.walk(args.open_model_zoo, topdown=False):
    if 'models.lst' in files:
        with open(os.path.join(root, 'models.lst')) as f:
            demo_url = root.split('/')
            demo_url = '/'.join(demo_url[demo_url.index('demos'):])
            demo_url = '/'.join((OV_MODEL_ZOO_URL, demo_url))
            demo_app = os.path.basename(os.path.dirname(root))
            for line in f.read().splitlines():
                if line[0] == '#':
                    continue
                name_regexp = line.replace('?', '.').replace('-encoder','')
                for composite_name, m in models.items():
                    for name in m.get('stages_order', [composite_name]):
                        if re.match(name_regexp, name):
                            if 'demo_apps' in models[composite_name]:
                                models[composite_name]['demo_apps'].append(demo_app)
                            else:
                                models[composite_name]['demo_apps'] = [demo_app]

                            if 'demo_urls' in models[composite_name]:
                                models[composite_name]['demo_urls'].append(demo_url)
                            else:
                                models[composite_name]['demo_urls'] = [demo_url]

                            break

# read model_index.yaml
if args.model_index is not None:
    with open(args.model_index) as f:
        #model_index_schema = args.model_index.replace(".yaml", "_schema.json")
        #with open(model_index_schema) as mis:
            model_index = yaml.safe_load(f)
            #validate(model_index, json.load(mis))
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
                        deffective_models.append(name)
                    if 'format' not in m:
                        m['format'] = openvino_str
                    models[name] = m

# remove models that we don't want to include, e.g. preview/deprecation
models_to_remove = ['icnet-camvid-ava-0001', 'road-segmentation-adas-0001',
                    'semantic-segmentation-adas-0001', 'text-detection-0003']
for model_name in (models_to_remove + deffective_models):
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


models_filtered={}

for key, value in models.items():

    keys_to_del  = [k for k, v in value.items() if k[0:5]=='[COCO']

    for k in keys_to_del:
        value.pop(k)

    value.pop("Accuracy", None)
    value.pop("Metric", None)
    value.pop("module_config", None)
    value.pop("model_info", None)
    value.pop("Postprocessing", None)
    value.pop("postprocessing", None)
    value.pop("dlstreamer_gst_openvino", None)
    value.pop("Top 1", None)
    value.pop("Top 5", None)
    value.pop("conversion_to_onnx_args", None)
    value.pop("GFlops", None)
    value.pop("MParams", None)
    value.pop("datasets", None)
    value.pop("Dataset", None)
    value.pop("dlstreamer_support", None)
    value.pop("format", None)
    value.pop("framework", None)
    value.pop("input_info", None)
    value.pop("launchers", None)
    value.pop("mParams", None)
    value.pop("model_optimizer_args", None)
    tempv = value.get('files',[])
    if tempv != []:
        tempv = tempv[0].get('source', '')
    value.pop('files', None)

    models_filtered[key] = value

with open(args.output, 'w') as file:
    yaml.dump(models_filtered, file)


