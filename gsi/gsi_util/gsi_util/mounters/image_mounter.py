#!/usr/bin/env python
#
# Copyright 2017 - The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Provides class ImageMounter."""

import logging
import os
import shutil
import sys
import tempfile

import gsi_util.mounters.base_mounter as base_mounter
import gsi_util.utils.image_utils as image_utils


class _ImageFileAccessor(base_mounter.BaseFileAccessor):

  @staticmethod
  def _prepare_dirs_for_file(filename):
    dir_path = os.path.dirname(filename)
    try:
      os.makedirs(dir_path)
    except OSError as exc:
      if exc.errno != errno.EEXIST:
        raise

  def __init__(self, path_prefix, mount_point, temp_dir):
    super(_ImageFileAccessor, self).__init__(path_prefix)
    self._mount_point = mount_point
    self._temp_dir = temp_dir

  def _handle_prepare_file(self, pathfile_in_mount):
    mount_filename = os.path.join(self._mount_point, pathfile_in_mount)
    filename = os.path.join(self._temp_dir, pathfile_in_mount)
    logging.info('Prepare file %s -> %s', pathfile_in_mount, filename)
    if not os.path.isfile(mount_filename):
      logging.error('File is not exist: %s', pathfile_in_mount)
      return None

    self._prepare_dirs_for_file(filename)
    image_utils.copy_file(filename, mount_filename)

    def cleanup():
      logging.debug('Delete temp file: %s', filename)
      os.remove(filename)

    return (filename, cleanup)


class ImageMounter(base_mounter.BaseMounter):
  """Provides a file accessor which can access files in the given image file."""

  _SYSTEM_FILES = ['compatibility_matrix.xml', 'build.prop', 'manifest.xml']

  def __init__(self, image_filename, path_prefix):
    super(ImageMounter, self).__init__()
    self._image_filename = image_filename
    self._path_prefix = path_prefix

  @classmethod
  def _detect_system_as_root(cls, mount_point):
    """Return True if the image layout on mount_point is system-as-root."""
    logging.debug('Checking system-as-root on mount point %s...', mount_point)

    system_without_root = True
    for filename in cls._SYSTEM_FILES:
      if not os.path.isfile(os.path.join(mount_point, filename)):
        system_without_root = False
        break

    system_as_root = True
    for filename in cls._SYSTEM_FILES:
      if not os.path.isfile(os.path.join(mount_point, 'system', filename)):
        system_as_root = False
        break

    ret = system_as_root and not system_without_root
    logging.debug('  Result=%s', ret)
    return ret

  def _handle_mount(self):
    # Unsparse the image to a temp file
    unsparsed_suffix = '_system.img.raw'
    unsparsed_file = tempfile.NamedTemporaryFile(suffix=unsparsed_suffix)
    unsparsed_filename = unsparsed_file.name
    image_utils.unsparse(unsparsed_filename, self._image_filename)

    # Mount it
    mount_point = tempfile.mkdtemp()
    logging.debug('Create a temp mount point %s', mount_point)
    image_utils.mount(mount_point, unsparsed_filename)

    # detect system-as-root if need
    path_prefix = self._path_prefix
    if path_prefix == 'detect-system-as-root':
      path_prefix = '/' if self._detect_system_as_root(
          mount_point) else '/system/'

    # Create a temp dir for the target of copying file from image
    temp_dir = tempfile.mkdtemp()
    logging.debug('Created temp dir: %s', temp_dir)

    # Keep data to be removed on __exit__
    self._unsparsed_file = unsparsed_file
    self._mount_point = mount_point
    self._temp_dir = tempfile.mkdtemp()

    return _ImageFileAccessor(path_prefix, mount_point, temp_dir)

  def _handle_unmount(self):
    if self._temp_dir:
      logging.debug('Remove temp dir: %s', self._temp_dir)
      shutil.rmtree(self._temp_dir)
      del self._temp_dir

    if self._mount_point:
      image_utils.unmount(self._mount_point)
      shutil.rmtree(self._mount_point)
      del self._mount_point

    del self._unsparsed_file
