# Copyright 2018 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Blink feature-policy presubmit script.

See http://dev.chromium.org/developers/how-tos/depottools/presubmit-scripts
for more details about the presubmit API built into gcl.
"""


import os


def _dynamic_import(module_dir, module_name):
    import sys

    original_sys_path = sys.path
    try:
        sys.path = sys.path + [module_dir]
        return __import__(module_name)
    finally:
        sys.path = original_sys_path


def _import_update_histogram_enum(input_api):
    return _dynamic_import(
        input_api.os_path.join(input_api.PresubmitLocalPath(), '..', '..',
                               '..', '..', '..', 'tools', 'metrics',
                               'histograms'), 'update_histogram_enum')


def _import_path_util(input_api):
    return _dynamic_import(
        input_api.os_path.join(input_api.PresubmitLocalPath(), '..', '..',
                               '..', '..', '..', 'tools', 'metrics', 'common'),
        'path_util')


# Note: this function is copied from third_party/blink/renderer/build/scripts/json5_generator
def _json5_load(lines):
    import re
    import ast
    # Use json5.loads when json5 is available. Currently we use simple
    # regexs to convert well-formed JSON5 to PYL format.
    # Strip away comments and quote unquoted keys.
    re_comment = re.compile(r"^\s*//.*$|//+ .*$", re.MULTILINE)
    re_map_keys = re.compile(r"^\s*([$A-Za-z_][\w]*)\s*:", re.MULTILINE)
    pyl = re.sub(re_map_keys, r"'\1':", re.sub(re_comment, "", lines))
    # Convert map values of true/false to Python version True/False.
    re_true = re.compile(r":\s*true\b")
    re_false = re.compile(r":\s*false\b")
    pyl = re.sub(re_true, ":True", re.sub(re_false, ":False", pyl))
    return ast.literal_eval(pyl)


def _json5_load_from_file(file_path):
    with open(file_path, 'r') as f:
        return _json5_load(f.read())


def _RunUmaHistogramChecks(input_api, output_api):  # pylint: disable=C0103
    source_path = ''
    for f in input_api.AffectedFiles():
        if f.LocalPath().endswith('feature_policy_feature.mojom'):
            source_path = f.LocalPath()
            break
    else:
        return []

    start_marker = '^enum FeaturePolicyFeature {'
    end_marker = '^};'
    presubmit_error = _import_update_histogram_enum(
        input_api).CheckPresubmitErrors(
            histogram_enum_name='FeaturePolicyFeature',
            update_script_name='update_feature_policy_enum.py',
            source_enum_path=source_path,
            start_marker=start_marker,
            end_marker=end_marker,
            strip_k_prefix=True)
    if presubmit_error:
        return [
            output_api.PresubmitPromptWarning(
                presubmit_error, items=[source_path])
        ]
    return []


def _RunJson5ConfigChecks(input_api, output_api):  # pylint: disable=C0103
    mojom_source_path = os.path.join('third_party', 'blink', 'public', 'mojom',
                                     'feature_policy',
                                     'feature_policy_feature.mojom')
    json5_config_path = os.path.join('third_party', 'blink', 'renderer',
                                     'core', 'feature_policy',
                                     'feature_policy_features.json5')

    affected_paths = {f.LocalPath() for f in input_api.AffectedFiles()}
    if mojom_source_path not in affected_paths and json5_config_path not in affected_paths:
        return []

    mojom_enums = set(
        _import_update_histogram_enum(input_api).ReadHistogramValues(
            mojom_source_path,
            start_marker='^enum FeaturePolicyFeature {',
            end_marker='^};',
            strip_k_prefix=True).values()) - {'NotFound'}

    json5_enums = {
        feature['name']
        for feature in _json5_load_from_file(
            _import_path_util(input_api).GetInputFile(json5_config_path))
        ['data']
    }

    json5_missing_enums = mojom_enums - json5_enums
    mojom_missing_enums = json5_enums - mojom_enums

    json5_messages = "{} are missing in json5 config.\n".format(
        list(json5_missing_enums)) if json5_missing_enums else ""
    mojom_messages = "{} are missing in mojom file.\n".format(
        list(mojom_missing_enums)) if mojom_missing_enums else ""

    return [] if json5_enums == mojom_source_path else [
        output_api.PresubmitPromptWarning(
            "{} and {} are out of sync: {}{}".format(
                json5_config_path, mojom_source_path, json5_messages,
                mojom_messages),
            items=[mojom_source_path, json5_config_path])
    ]


def CheckChangeOnUpload(input_api, output_api):  # pylint: disable=C0103
    results = []
    results.extend(_RunUmaHistogramChecks(input_api, output_api))
    results.extend(_RunJson5ConfigChecks(input_api, output_api))
    return results


def CheckChangeOnCommit(input_api, output_api):  # pylint: disable=C0103
    results = []
    results.extend(_RunUmaHistogramChecks(input_api, output_api))
    results.extend(_RunJson5ConfigChecks(input_api, output_api))
    return results
