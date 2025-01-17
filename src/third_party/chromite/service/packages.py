# -*- coding: utf-8 -*-
# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Package utility functionality."""

from __future__ import print_function

import collections
import fileinput
import functools
import json
import os
import re
import sys

import six

from google.protobuf import json_format

from chromite.api.gen.config import replication_config_pb2
from chromite.cbuildbot import manifest_version
from chromite.lib import constants
from chromite.lib import cros_build_lib
from chromite.lib import cros_logging as logging
from chromite.lib import git
from chromite.lib import image_lib
from chromite.lib import osutils
from chromite.lib import portage_util
from chromite.lib import replication_lib
from chromite.lib import uprev_lib

if cros_build_lib.IsInsideChroot():
  from chromite.service import dependency


# Registered handlers for uprevving versioned packages.
_UPREV_FUNCS = {}


class Error(Exception):
  """Module's base error class."""


class UnknownPackageError(Error):
  """Uprev attempted for a package without a registered handler."""


class UprevError(Error):
  """An error occurred while uprevving packages."""


class NoAndroidVersionError(Error):
  """An error occurred while trying to determine the android version."""


class NoAndroidBranchError(Error):
  """An error occurred while trying to determine the android branch."""


class NoAndroidTargetError(Error):
  """An error occurred while trying to determine the android target."""


class AndroidIsPinnedUprevError(UprevError):
  """Raised when we try to uprev while Android is pinned."""

  def __init__(self, new_android_atom):
    """Initialize a AndroidIsPinnedUprevError.

    Args:
      new_android_atom: The Android atom that we failed to
                        uprev to, due to Android being pinned.
    """
    assert new_android_atom
    msg = ('Failed up uprev to Android version %s as Android was pinned.' %
           new_android_atom)
    super(AndroidIsPinnedUprevError, self).__init__(msg)
    self.new_android_atom = new_android_atom


class EbuildManifestError(Error):
  """Error when running ebuild manifest."""


class GeneratedCrosConfigFilesError(Error):
  """Error when cros_config_schema does not produce expected files"""

  def __init__(self, expected_files, found_files):
    msg = ('Expected to find generated C files: %s. Actually found: %s' %
           (expected_files, found_files))
    super(GeneratedCrosConfigFilesError, self).__init__(msg)


UprevVersionedPackageModifications = collections.namedtuple(
    'UprevVersionedPackageModifications', ('new_version', 'files'))


class UprevVersionedPackageResult(object):
  """Data object for uprev_versioned_package."""

  def __init__(self):
    self.modified = []

  def add_result(self, new_version, modified_files):
    """Adds version/ebuilds tuple to result.

    Args:
      new_version: New version number of package.
      modified_files: List of files modified for the given version.
    """
    result = UprevVersionedPackageModifications(new_version, modified_files)
    self.modified.append(result)
    return self

  @property
  def uprevved(self):
    return bool(self.modified)


def patch_ebuild_vars(ebuild_path, variables):
  """Updates variables in ebuild.

  Use this function rather than portage_util.EBuild.UpdateEBuild when you
  want to preserve the variable position and quotes within the ebuild.

  Args:
    ebuild_path: The path of the ebuild.
    variables: Dictionary of variables to update in ebuild.
  """
  try:
    for line in fileinput.input(ebuild_path, inplace=1):
      varname, eq, _ = line.partition('=')
      if eq == '=' and varname.strip() in variables:
        value = variables[varname]
        sys.stdout.write('%s="%s"\n' % (varname, value))
      else:
        sys.stdout.write(line)
  finally:
    fileinput.close()


def uprevs_versioned_package(package):
  """Decorator to register package uprev handlers."""
  assert package

  def register(func):
    """Registers |func| as a handler for |package|."""
    _UPREV_FUNCS[package] = func

    @functools.wraps(func)
    def pass_through(*args, **kwargs):
      return func(*args, **kwargs)

    return pass_through

  return register


def uprev_android(tracking_branch,
                  android_package,
                  android_build_branch,
                  chroot,
                  build_targets=None,
                  android_version=None):
  """Returns the portage atom for the revved Android ebuild - see man emerge."""
  command = [
      'cros_mark_android_as_stable',
      '--tracking_branch=%s' % tracking_branch,
      '--android_package=%s' % android_package,
      '--android_build_branch=%s' % android_build_branch,
  ]
  if build_targets:
    command.append('--boards=%s' % ':'.join(bt.name for bt in build_targets))
  if android_version:
    command.append('--force_version=%s' % android_version)

  result = cros_build_lib.run(
      command,
      stdout=True,
      enter_chroot=True,
      encoding='utf-8',
      chroot_args=chroot.get_enter_args())

  portage_atom_string = result.stdout.strip()
  android_atom = None
  if portage_atom_string:
    android_atom = portage_atom_string.splitlines()[-1].partition('=')[-1]
  if not android_atom:
    logging.info('Found nothing to rev.')
    return None

  for target in build_targets or []:
    # Sanity check: We should always be able to merge the version of
    # Android we just unmasked.
    command = ['emerge-%s' % target.name, '-p', '--quiet', '=%s' % android_atom]
    try:
      cros_build_lib.run(
          command, enter_chroot=True, chroot_args=chroot.get_enter_args())
    except cros_build_lib.RunCommandError:
      logging.error(
          'Cannot emerge-%s =%s\nIs Android pinned to an older '
          'version?', target, android_atom)
      raise AndroidIsPinnedUprevError(android_atom)

  return android_atom


def uprev_build_targets(build_targets,
                        overlay_type,
                        chroot=None,
                        output_dir=None):
  """Uprev the set provided build targets, or all if not specified.

  Args:
    build_targets (list[build_target_lib.BuildTarget]|None): The build targets
      whose overlays should be uprevved, empty or None for all.
    overlay_type (str): One of the valid overlay types except None (see
      constants.VALID_OVERLAYS).
    chroot (chroot_lib.Chroot|None): The chroot to clean, if desired.
    output_dir (str|None): The path to optionally dump result files.
  """
  # Need a valid overlay, but exclude None.
  assert overlay_type and overlay_type in constants.VALID_OVERLAYS

  if build_targets:
    overlays = portage_util.FindOverlaysForBoards(
        overlay_type, boards=[t.name for t in build_targets])
  else:
    overlays = portage_util.FindOverlays(overlay_type)

  return uprev_overlays(
      overlays,
      build_targets=build_targets,
      chroot=chroot,
      output_dir=output_dir)


def uprev_overlays(overlays, build_targets=None, chroot=None, output_dir=None):
  """Uprev the given overlays.

  Args:
    overlays (list[str]): The list of overlay paths.
    build_targets (list[build_target_lib.BuildTarget]|None): The build targets
      to clean in |chroot|, if desired. No effect unless |chroot| is provided.
    chroot (chroot_lib.Chroot|None): The chroot to clean, if desired.
    output_dir (str|None): The path to optionally dump result files.

  Returns:
    list[str] - The paths to all of the modified ebuild files. This includes the
      new files that were added (i.e. the new versions) and all of the removed
      files (i.e. the old versions).
  """
  assert overlays

  manifest = git.ManifestCheckout.Cached(constants.SOURCE_ROOT)

  uprev_manager = uprev_lib.UprevOverlayManager(
      overlays,
      manifest,
      build_targets=build_targets,
      chroot=chroot,
      output_dir=output_dir)
  uprev_manager.uprev()

  return uprev_manager.modified_ebuilds


def uprev_versioned_package(package, build_targets, refs, chroot):
  """Call registered uprev handler function for the package.

  Args:
    package (portage_util.CPV): The package being uprevved.
    build_targets (list[build_target_lib.BuildTarget]): The build targets to
        clean on a successful uprev.
    refs (list[uprev_lib.GitRef]):
    chroot (chroot_lib.Chroot): The chroot to enter for cleaning.

  Returns:
    UprevVersionedPackageResult: The result.
  """
  assert package

  if package.cp not in _UPREV_FUNCS:
    raise UnknownPackageError(
        'Package "%s" does not have a registered handler.' % package.cp)

  return _UPREV_FUNCS[package.cp](build_targets, refs, chroot)


@uprevs_versioned_package('media-libs/virglrenderer')
def uprev_virglrenderer(_build_targets, refs, _chroot):
  """Updates virglrenderer ebuilds.

  See: uprev_versioned_package.

  Returns:
    UprevVersionedPackageResult: The result of updating virglrenderer ebuilds.
  """
  overlay = os.path.join(constants.SOURCE_ROOT,
                         constants.CHROMIUMOS_OVERLAY_DIR)
  repo_path = os.path.join(constants.SOURCE_ROOT, 'src', 'third_party',
                           'virglrenderer')
  manifest = git.ManifestCheckout.Cached(repo_path)

  uprev_manager = uprev_lib.UprevOverlayManager([overlay], manifest)
  # TODO(crbug.com/1066242): Ebuilds for virglrenderer are currently
  # blacklisted. Do not force uprevs after builder is stable and ebuilds are no
  # longer blacklisted.
  uprev_manager.uprev(package_list=['media-libs/virglrenderer'], force=True)

  updated_files = uprev_manager.modified_ebuilds
  result = UprevVersionedPackageResult()
  result.add_result(refs[0].revision, updated_files)
  return result


@uprevs_versioned_package('afdo/kernel-profiles')
def uprev_kernel_afdo(*_args, **_kwargs):
  """Updates kernel ebuilds with versions from kernel_afdo.json.

  See: uprev_versioned_package.

  Raises:
    EbuildManifestError: When ebuild manifest does not complete successfuly.
  """
  path = os.path.join(constants.SOURCE_ROOT, 'src', 'third_party',
                      'toolchain-utils', 'afdo_metadata', 'kernel_afdo.json')

  with open(path, 'r') as f:
    versions = json.load(f)

  result = UprevVersionedPackageResult()
  for version, version_info in versions.items():
    path = os.path.join('src', 'third_party', 'chromiumos-overlay',
                        'sys-kernel', version)
    ebuild_path = os.path.join(constants.SOURCE_ROOT, path,
                               '%s-9999.ebuild' % version)
    chroot_ebuild_path = os.path.join(constants.CHROOT_SOURCE_ROOT, path,
                                      '%s-9999.ebuild' % version)
    afdo_profile_version = version_info['name']
    patch_ebuild_vars(ebuild_path,
                      dict(AFDO_PROFILE_VERSION=afdo_profile_version))

    try:
      cmd = ['ebuild', chroot_ebuild_path, 'manifest', '--force']
      cros_build_lib.run(cmd, enter_chroot=True)
    except cros_build_lib.RunCommandError as e:
      raise EbuildManifestError(
          'Error encountered when regenerating the manifest for ebuild: %s\n%s'
          % (chroot_ebuild_path, e), e)

    manifest_path = os.path.join(constants.SOURCE_ROOT, path, 'Manifest')

    result.add_result(afdo_profile_version, [ebuild_path, manifest_path])

  return result


@uprevs_versioned_package('chromeos-base/termina-image-amd64')
def uprev_termina_amd64(_build_targets, _refs, chroot):
  """Updates termina amd64 VM - chromeos-base/termina-image-amd64.

  See: uprev_versioned_package.
  """
  return uprev_termina('termina-image-amd64', chroot)


@uprevs_versioned_package('chromeos-base/termina-image-arm')
def uprev_termina_arm(_build_targets, _refs, chroot):
  """Updates termina arm VM - chromeos-base/termina-image-arm.

  See: uprev_versioned_package.
  """
  return uprev_termina('termina-image-arm', chroot)


def uprev_termina(package, chroot):
  """Helper function to uprev termina VM.

  Args:
    package (string): name of the package
    chroot (chroot_lib.Chroot): specify a chroot to enter.

  Returns:
    UprevVersionedPackageResult: The result.
  """
  package_path = os.path.join(constants.CHROMIUMOS_OVERLAY_DIR, 'chromeos-base',
                              package)
  version_pin_path = os.path.join(package_path, 'VERSION-PIN')
  return uprev_ebuild_from_pin(package_path, version_pin_path, chroot)


@uprevs_versioned_package('chromeos-base/chromeos-dtc-vm')
def uprev_sludge(_build_targets, _refs, chroot):
  """Updates sludge VM - chromeos-base/chromeos-dtc-vm.

  See: uprev_versioned_package.
  """
  package = 'chromeos-dtc-vm'
  package_path = os.path.join('src', 'private-overlays',
                              'project-wilco-private', 'chromeos-base', package)
  version_pin_path = os.path.join(package_path, 'VERSION-PIN')

  return uprev_ebuild_from_pin(package_path, version_pin_path, chroot)


def uprev_ebuild_from_pin(package_path, version_pin_path, chroot):
  """Changes the package ebuild's version to match the version pin file.

  Args:
    package_path: The path of the package relative to the src root. This path
      should contain a single ebuild with the same name as the package.
    version_pin_path: The path of the version_pin file that contains only a
      version string. The ebuild's version will be directly set to this
      number.
    chroot (chroot_lib.Chroot): specify a chroot to enter.

  Returns:
    UprevVersionedPackageResult: The result.
  """
  package = os.path.basename(package_path)

  package_src_path = os.path.join(constants.SOURCE_ROOT, package_path)
  ebuild_paths = list(portage_util.EBuild.List(package_src_path))
  if not ebuild_paths:
    raise UprevError('No ebuilds found for %s' % package)
  elif len(ebuild_paths) > 1:
    raise UprevError('Multiple ebuilds found for %s' % package)
  else:
    ebuild_path = ebuild_paths[0]

  version_pin_src_path = os.path.join(constants.SOURCE_ROOT, version_pin_path)
  version = osutils.ReadFile(version_pin_src_path).strip()
  new_ebuild_path = os.path.join(package_path,
                                 '%s-%s-r1.ebuild' % (package, version))
  new_ebuild_src_path = os.path.join(constants.SOURCE_ROOT, new_ebuild_path)
  os.rename(ebuild_path, new_ebuild_src_path)
  manifest_src_path = os.path.join(package_src_path, 'Manifest')
  new_ebuild_chroot_path = os.path.join(constants.CHROOT_SOURCE_ROOT,
                                        new_ebuild_path)

  try:
    portage_util.UpdateEbuildManifest(new_ebuild_chroot_path, chroot=chroot)
  except cros_build_lib.RunCommandError as e:
    raise EbuildManifestError(
        'Unable to update manifest for %s: %s' % (package, e.stderr))

  result = UprevVersionedPackageResult()
  result.add_result(version,
                    [new_ebuild_src_path, ebuild_path, manifest_src_path])
  return result


@uprevs_versioned_package(constants.CHROME_CP)
def uprev_chrome(build_targets, refs, chroot):
  """Uprev chrome and its related packages.

  See: uprev_versioned_package.
  """
  # Determine the version from the refs (tags), i.e. the chrome versions are the
  # tag names.
  chrome_version = uprev_lib.get_chrome_version_from_refs(refs)
  logging.debug('Chrome version determined from refs: %s', chrome_version)

  uprev_manager = uprev_lib.UprevChromeManager(
      chrome_version, build_targets=build_targets, chroot=chroot)
  result = UprevVersionedPackageResult()
  # Start with chrome itself, as we can't do anything else unless chrome
  # uprevs successfully.
  # TODO(crbug.com/1080429): Handle all possible outcomes of a Chrome uprev
  #     attempt. The expected behavior is documented in the following table:
  #
  #     Outcome of Chrome uprev attempt:
  #     NEWER_VERSION_EXISTS:
  #       Do nothing.
  #     SAME_VERSION_EXISTS or REVISION_BUMP:
  #       Uprev followers
  #       Assert not VERSION_BUMP (any other outcome is fine)
  #     VERSION_BUMP or NEW_EBUILD_CREATED:
  #       Uprev followers
  #       Assert that Chrome & followers are at same package version
  if not uprev_manager.uprev(constants.CHROME_CP):
    return result

  # With a successful chrome rev, also uprev related packages.
  for package in constants.OTHER_CHROME_PACKAGES:
    uprev_manager.uprev(package)

  return result.add_result(chrome_version, uprev_manager.modified_ebuilds)


def _generate_platform_c_files(replication_config, chroot):
  """Generates platform C files from a platform JSON payload.

  Args:
    replication_config (replication_config_pb2.ReplicationConfig): A
      ReplicationConfig that has already been run. If it produced a
      build_config.json file, that file will be used to generate platform C
      files. Otherwise, nothing will be generated.
    chroot (chroot_lib.Chroot): The chroot to use to generate.

  Returns:
    A list of generated files.
  """
  # Generate the platform C files from the build config. Note that it would be
  # more intuitive to generate the platform C files from the platform config;
  # however, cros_config_schema does not allow this, because the platform config
  # payload is not always valid input. For example, if a property is both
  # 'required' and 'build-only', it will fail schema validation. Thus, use the
  # build config, and use '-f' to filter.
  build_config_path = [
      rule.destination_path
      for rule in replication_config.file_replication_rules
      if rule.destination_path.endswith('build_config.json')
  ]

  if not build_config_path:
    logging.info(
        'No build_config.json found, will not generate platform C files. '
        'Replication config: %s', replication_config)
    return []

  if len(build_config_path) > 1:
    raise ValueError('Expected at most one build_config.json destination path. '
                     'Replication config: %s' % replication_config)

  build_config_path = build_config_path[0]

  # Paths to the build_config.json and dir to output C files to, in the
  # chroot.
  build_config_chroot_path = os.path.join(constants.CHROOT_SOURCE_ROOT,
                                          build_config_path)
  generated_output_chroot_dir = os.path.join(constants.CHROOT_SOURCE_ROOT,
                                             os.path.dirname(build_config_path))

  command = [
      'cros_config_schema', '-m', build_config_chroot_path, '-g',
      generated_output_chroot_dir, '-f', '"TRUE"'
  ]

  cros_build_lib.run(
      command, enter_chroot=True, chroot_args=chroot.get_enter_args())

  # A relative (to the source root) path to the generated C files.
  generated_output_dir = os.path.dirname(build_config_path)
  generated_files = []
  expected_c_files = ['config.c', 'ec_config.c', 'ec_config.h']
  for f in expected_c_files:
    if os.path.exists(
        os.path.join(constants.SOURCE_ROOT, generated_output_dir, f)):
      generated_files.append(os.path.join(generated_output_dir, f))

  if len(expected_c_files) != len(generated_files):
    raise GeneratedCrosConfigFilesError(expected_c_files, generated_files)

  return generated_files


def _get_private_overlay_package_root(ref, package):
  """Returns the absolute path to the root of a given private overlay.

  Args:
    ref (uprev_lib.GitRef): GitRef for the private overlay.
    package (str): Path to the package in the overlay.
  """
  # There might be a cleaner way to map from package -> path within the source
  # tree. For now, just use string patterns.
  private_overlay_ref_pattern = r'/chromeos\/overlays\/overlay-([\w-]+)-private'
  match = re.match(private_overlay_ref_pattern, ref.path)
  if not match:
    raise ValueError('ref.path must match the pattern: %s. Actual ref: %s' %
                     (private_overlay_ref_pattern, ref))

  overlay = match.group(1)

  return os.path.join(constants.SOURCE_ROOT,
                      'src/private-overlays/overlay-%s-private' % overlay,
                      package)


@uprevs_versioned_package('chromeos-base/chromeos-config-bsp')
def replicate_private_config(_build_targets, refs, chroot):
  """Replicate a private cros_config change to the corresponding public config.

  See uprev_versioned_package for args
  """
  package = 'chromeos-base/chromeos-config-bsp'

  if len(refs) != 1:
    raise ValueError('Expected exactly one ref, actual %s' % refs)

  # Expect a replication_config.jsonpb in the package root.
  package_root = _get_private_overlay_package_root(refs[0], package)
  replication_config_path = os.path.join(package_root,
                                         'replication_config.jsonpb')

  try:
    replication_config = json_format.Parse(
        osutils.ReadFile(replication_config_path),
        replication_config_pb2.ReplicationConfig())
  except IOError:
    raise ValueError(
        'Expected ReplicationConfig missing at %s' % replication_config_path)

  replication_lib.Replicate(replication_config)

  modified_files = [
      rule.destination_path
      for rule in replication_config.file_replication_rules
  ]

  # The generated platform C files are not easily filtered by replication rules,
  # i.e. JSON / proto filtering can be described by a FieldMask, arbitrary C
  # files cannot. Therefore, replicate and filter the JSON payloads, and then
  # generate filtered C files from the JSON payload.
  modified_files.extend(_generate_platform_c_files(replication_config, chroot))

  # Use the private repo's commit hash as the new version.
  new_private_version = refs[0].revision

  # modified_files should contain only relative paths at this point, but the
  # returned UprevVersionedPackageResult must contain only absolute paths.
  for i, modified_file in enumerate(modified_files):
    assert not os.path.isabs(modified_file)
    modified_files[i] = os.path.join(constants.SOURCE_ROOT, modified_file)

  return UprevVersionedPackageResult().add_result(new_private_version,
                                                  modified_files)


def get_best_visible(atom, build_target=None):
  """Returns the best visible CPV for the given atom.

  Args:
    atom (str): The atom to look up.
    build_target (build_target_lib.BuildTarget): The build target whose
        sysroot should be searched, or the SDK if not provided.

  Returns:
    portage_util.CPV|None: The best visible package.
  """
  assert atom

  board = build_target.name if build_target else None
  return portage_util.PortageqBestVisible(atom, board=board)


def has_prebuilt(atom, build_target=None, useflags=None):
  """Check if a prebuilt exists.

  Args:
    atom (str): The package whose prebuilt is being queried.
    build_target (build_target_lib.BuildTarget): The build target whose
        sysroot should be searched, or the SDK if not provided.
    useflags: Any additional USE flags that should be set. May be a string
        of properly formatted USE flags, or an iterable of individual flags.

  Returns:
    bool: True iff there is an available prebuilt, False otherwise.
  """
  assert atom

  board = build_target.name if build_target else None
  extra_env = None
  if useflags:
    new_flags = useflags
    if not isinstance(useflags, six.string_types):
      new_flags = ' '.join(useflags)

    existing = os.environ.get('USE', '')
    final_flags = '%s %s' % (existing, new_flags)
    extra_env = {'USE': final_flags.strip()}
  return portage_util.HasPrebuilt(atom, board=board, extra_env=extra_env)


def builds(atom, build_target, packages=None):
  """Check if |build_target| builds |atom| (has it in its depgraph)."""
  cros_build_lib.AssertInsideChroot()

  # TODO(crbug/1081828): Receive and use sysroot.
  graph, _sdk_graph = dependency.GetBuildDependency(
      build_target.root, build_target.name, packages)
  return any(atom in package for package in graph['package_deps'])


def determine_chrome_version(build_target):
  """Returns the current Chrome version for the board (or in buildroot).

  Args:
    build_target (build_target_lib.BuildTarget): The board build target.

  Returns:
    str|None: The chrome version if available.
  """
  # TODO(crbug/1019770): Long term we should not need the try/catch here once
  # the builds function above only returns True for chrome when
  # determine_chrome_version will succeed.
  try:
    cpv = portage_util.PortageqBestVisible(
        constants.CHROME_CP, build_target.name, cwd=constants.SOURCE_ROOT)
  except cros_build_lib.RunCommandError as e:
    # Return None because portage failed when trying to determine the chrome
    # version.
    logging.warning('Caught exception in determine_chrome_package: %s', e)
    return None
  # Something like 78.0.3877.4_rc -> 78.0.3877.4
  return cpv.version_no_rev.partition('_')[0]


def determine_android_package(board):
  """Returns the active Android container package in use by the board.

  Args:
    board: The board name this is specific to.

  Returns:
    str|None: The android package string if there is one.
  """
  try:
    packages = portage_util.GetPackageDependencies(board, 'virtual/target-os')
  except cros_build_lib.RunCommandError as e:
    # Return None because a command (likely portage) failed when trying to
    # determine the package.
    logging.warning('Caught exception in determine_android_package: %s', e)
    return None

  # We assume there is only one Android package in the depgraph.
  for package in packages:
    if package.startswith('chromeos-base/android-container-') or \
        package.startswith('chromeos-base/android-vm-'):
      return package
  return None


def determine_android_version(boards=None):
  """Determine the current Android version in buildroot now and return it.

  This uses the typical portage logic to determine which version of Android
  is active right now in the buildroot.

  Args:
    boards: List of boards to check version of.

  Returns:
    The Android build ID of the container for the boards.

  Raises:
    NoAndroidVersionError: if no unique Android version can be determined.
  """
  if not boards:
    return None
  # Verify that all boards have the same version.
  version = None
  for board in boards:
    package = determine_android_package(board)
    if not package:
      return None
    cpv = portage_util.SplitCPV(package)
    if not cpv:
      raise NoAndroidVersionError(
          'Android version could not be determined for %s' % board)
    if not version:
      version = cpv.version_no_rev
    elif version != cpv.version_no_rev:
      raise NoAndroidVersionError('Different Android versions (%s vs %s) for %s'
                                  % (version, cpv.version_no_rev, boards))
  return version


def determine_android_branch(board):
  """Returns the Android branch in use by the active container ebuild."""
  try:
    android_package = determine_android_package(board)
  except cros_build_lib.RunCommandError:
    raise NoAndroidBranchError(
        'Android branch could not be determined for %s' % board)
  if not android_package:
    return None
  ebuild_path = portage_util.FindEbuildForBoardPackage(android_package, board)
  # We assume all targets pull from the same branch and that we always
  # have at least one of the following targets.
  targets = constants.ANDROID_ALL_BUILD_TARGETS
  ebuild_content = osutils.SourceEnvironment(ebuild_path, targets)
  for target in targets:
    if target in ebuild_content:
      branch = re.search(r'(.*?)-linux-', ebuild_content[target])
      if branch is not None:
        return branch.group(1)
  raise NoAndroidBranchError(
      'Android branch could not be determined for %s (ebuild empty?)' % board)


def determine_android_target(board):
  """Returns the Android target in use by the active container ebuild."""
  try:
    android_package = determine_android_package(board)
  except cros_build_lib.RunCommandError:
    raise NoAndroidTargetError(
        'Android Target could not be determined for %s' % board)
  if not android_package:
    return None
  if android_package.startswith('chromeos-base/android-vm-'):
    return 'bertha'
  elif android_package.startswith('chromeos-base/android-container-'):
    return 'cheets'

  raise NoAndroidTargetError(
      'Android Target cannot be determined for the package: %s' %
      android_package)


def determine_platform_version():
  """Returns the platform version from the source root."""
  # Platform version is something like '12575.0.0'.
  version = manifest_version.VersionInfo.from_repo(constants.SOURCE_ROOT)
  return version.VersionString()


def determine_milestone_version():
  """Returns the platform version from the source root."""
  # Milestone version is something like '79'.
  version = manifest_version.VersionInfo.from_repo(constants.SOURCE_ROOT)
  return version.chrome_branch


def determine_full_version():
  """Returns the full version from the source root."""
  # Full version is something like 'R79-12575.0.0'.
  milestone_version = determine_milestone_version()
  platform_version = determine_platform_version()
  full_version = ('R%s-%s' % (milestone_version, platform_version))
  return full_version


def find_fingerprints(build_target):
  """Returns a list of fingerprints for this build.

  Args:
    build_target (build_target_lib.BuildTarget): The build target.

  Returns:
    list[str] - List of fingerprint strings.
  """
  cros_build_lib.AssertInsideChroot()
  fp_file = 'cheets-fingerprint.txt'
  fp_path = os.path.join(
      image_lib.GetLatestImageLink(build_target.name),
      fp_file)
  if not os.path.isfile(fp_path):
    logging.info('Fingerprint file not found: %s', fp_path)
    return []
  logging.info('Reading fingerprint file: %s', fp_path)
  fingerprints = osutils.ReadFile(fp_path).splitlines()
  return fingerprints


def get_all_firmware_versions(build_target):
  """Extract firmware version for all models present.

  Args:
    build_target (build_target_lib.BuildTarget): The build target.

  Returns:
    A dict of FirmwareVersions namedtuple instances by model.
    Each element will be populated based on whether it was present in the
    command output.
  """
  cros_build_lib.AssertInsideChroot()
  result = {}
  # Note that example output for _get_firmware_version_cmd_result is available
  # in the packages_unittest.py for testing get_all_firmware_versions.
  cmd_result = _get_firmware_version_cmd_result(build_target)

  # There is a blank line between the version info for each model.
  firmware_version_payloads = cmd_result.split('\n\n')
  for firmware_version_payload in firmware_version_payloads:
    if 'BIOS' in firmware_version_payload:
      firmware_version = _find_firmware_versions(firmware_version_payload)
      result[firmware_version.model] = firmware_version
  return result


FirmwareVersions = collections.namedtuple(
    'FirmwareVersions', ['model', 'main', 'main_rw', 'ec', 'ec_rw'])


def get_firmware_versions(build_target):
  """Extract version information from the firmware updater, if one exists.

  Args:
    build_target (build_target_lib.BuildTarget): The build target.

  Returns:
    A FirmwareVersions namedtuple instance.
    Each element will either be set to the string output by the firmware
    updater shellball, or None if there is no firmware updater.
  """
  cros_build_lib.AssertInsideChroot()
  cmd_result = _get_firmware_version_cmd_result(build_target)
  if cmd_result:
    return _find_firmware_versions(cmd_result)
  else:
    return FirmwareVersions(None, None, None, None, None)


def _get_firmware_version_cmd_result(build_target):
  """Gets the raw result output of the firmware updater version command.

  Args:
    build_target (build_target_lib.BuildTarget): The build target.

  Returns:
    Command execution result.
  """
  updater = os.path.join(build_target.root,
                         'usr/sbin/chromeos-firmwareupdate')
  logging.info('Calling updater %s', updater)
  # Call the updater using the chroot-based path.
  return cros_build_lib.run([updater, '-V'],
                            capture_output=True, log_output=True,
                            encoding='utf-8').stdout


def _find_firmware_versions(cmd_output):
  """Finds firmware version output via regex matches against the cmd_output.

  Args:
    cmd_output: The raw output to search against.

  Returns:
    FirmwareVersions namedtuple with results.
    Each element will either be set to the string output by the firmware
    updater shellball, or None if there is no match.
  """

  # Sometimes a firmware bundle includes a special combination of RO+RW
  # firmware.  In this case, the RW firmware version is indicated with a "(RW)
  # version" field.  In other cases, the "(RW) version" field is not present.
  # Therefore, search for the "(RW)" fields first and if they aren't present,
  # fallback to the other format. e.g. just "BIOS version:".
  # TODO(mmortensen): Use JSON once the firmware updater supports it.
  main = None
  main_rw = None
  ec = None
  ec_rw = None
  model = None

  match = re.search(r'BIOS version:\s*(?P<version>.*)', cmd_output)
  if match:
    main = match.group('version')

  match = re.search(r'BIOS \(RW\) version:\s*(?P<version>.*)', cmd_output)
  if match:
    main_rw = match.group('version')

  match = re.search(r'EC version:\s*(?P<version>.*)', cmd_output)
  if match:
    ec = match.group('version')

  match = re.search(r'EC \(RW\) version:\s*(?P<version>.*)', cmd_output)
  if match:
    ec_rw = match.group('version')

  match = re.search(r'Model:\s*(?P<model>.*)', cmd_output)
  if match:
    model = match.group('model')

  return FirmwareVersions(model, main, main_rw, ec, ec_rw)


MainEcFirmwareVersions = collections.namedtuple(
    'MainEcFirmwareVersions', ['main_fw_version', 'ec_fw_version'])

def determine_firmware_versions(build_target):
  """Returns a namedtuple with main and ec firmware versions.

  Args:
    build_target (build_target_lib.BuildTarget): The build target.

  Returns:
    MainEcFirmwareVersions namedtuple with results.
  """
  fw_versions = get_firmware_versions(build_target)
  main_fw_version = fw_versions.main_rw or fw_versions.main
  ec_fw_version = fw_versions.ec_rw or fw_versions.ec

  return MainEcFirmwareVersions(main_fw_version, ec_fw_version)

def determine_kernel_version(build_target):
  """Returns a string containing the kernel version for this build target.

  Args:
    build_target (build_target_lib.BuildTarget): The build target.

  Returns:
    (str) The kernel versions, or None.
  """
  try:
    packages = portage_util.GetPackageDependencies(build_target.name,
                                                   'virtual/linux-sources')
  except cros_build_lib.RunCommandError as e:
    logging.warning('Unable to get package list for metadata: %s', e)
    return None
  for package in packages:
    if package.startswith('sys-kernel/chromeos-kernel-'):
      kernel_version = portage_util.SplitCPV(package).version
      logging.info('Found active kernel version: %s', kernel_version)
      return kernel_version
  return None


def get_models(build_target, log_output=True):
  """Obtain a list of models supported by a unified board.

  This ignored whitelabel models since GoldenEye has no specific support for
  these at present.

  Args:
    build_target (build_target_lib.BuildTarget): The build target.
    log_output: Whether to log the output of the cros_config_host invocation.

  Returns:
    A list of models supported by this board, if it is a unified build; None,
    if it is not a unified build.
  """
  return _run_cros_config_host(build_target, ['list-models'],
                               log_output=log_output)


def get_key_id(build_target, model):
  """Obtain the key_id for a model within the build_target.

  Args:
    build_target (build_target_lib.BuildTarget): The build target.
    model (str): The model name

  Returns:
    A key_id (str) or None.
  """
  model_arg = '--model=' + model
  key_id_list = _run_cros_config_host(
      build_target,
      [model_arg, 'get', '/firmware-signing', 'key-id'])
  key_id = None
  if len(key_id_list) == 1:
    key_id = key_id_list[0]
  return key_id


def _run_cros_config_host(build_target, args, log_output=True):
  """Run the cros_config_host tool.

  Args:
    build_target (build_target_lib.BuildTarget): The build target.
    args: List of arguments to pass.
    log_output: Whether to log the output of the cros_config_host.

  Returns:
    Output of the tool
  """
  cros_build_lib.AssertInsideChroot()
  tool = '/usr/bin/cros_config_host'
  if not os.path.isfile(tool):
    return None

  config_fname = build_target.full_path(
      'usr/share/chromeos-config/yaml/config.yaml')

  result = cros_build_lib.run(
      [tool, '-c', config_fname] + args,
      capture_output=True,
      encoding='utf-8',
      log_output=log_output,
      check=False)
  if result.returncode:
    # Show the output for debugging purposes.
    if 'No such file or directory' not in result.error:
      logging.error('cros_config_host failed: %s\n', result.error)
    return None
  return result.output.strip().splitlines()
