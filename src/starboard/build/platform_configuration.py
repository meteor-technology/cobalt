# Copyright 2017 The Cobalt Authors. All Rights Reserved.
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
"""Base platform build configuration."""

import imp  # pylint: disable=deprecated-module
import inspect
import logging
import os

import _env  # pylint: disable=unused-import, relative-import
from starboard.build.application_configuration import ApplicationConfiguration
from starboard.optional import get_optional_tests
from starboard.sabi import sabi
from starboard.tools import cache
from starboard.tools import environment
from starboard.tools import paths
from starboard.tools import platform
from starboard.tools.config import Config


def _GetApplicationConfigurationClass(application_path):
  """Gets the ApplicationConfiguration class from the given path."""
  if not os.path.isdir(application_path):
    return None

  application_configuration_path = os.path.join(application_path,
                                                'configuration.py')
  if not os.path.isfile(application_configuration_path):
    return None

  application_configuration = imp.load_source('application_configuration',
                                              application_configuration_path)
  for name, cls in inspect.getmembers(application_configuration):
    if not inspect.isclass(cls):
      continue
    if issubclass(cls, ApplicationConfiguration):
      logging.debug('Found ApplicationConfiguration: %s in %s', name,
                    application_configuration_path)
      return cls

  return None


class PlatformConfiguration(object):
  """Base platform build configuration class for all Starboard platforms.

  Should be derived by platform specific configurations.
  """

  def __init__(self,
               platform_name,
               asan_enabled_by_default=False,
               directory=None):
    self._platform_name = platform_name
    self._asan_default = 1 if asan_enabled_by_default else 0
    if directory:
      self._directory = directory
    else:
      self._directory = os.path.realpath(os.path.dirname(__file__))
    self._application_configuration = None
    self._application_configuration_search_path = [self._directory]
    # Default build accelerator is ccache.
    self.build_accelerator = self.GetBuildAccelerator(cache.Accelerator.CCACHE)

  def GetBuildAccelerator(self, accelerator):
    """Returns the build accelerator name."""
    build_accelerator = cache.Cache(accelerator)
    name = build_accelerator.GetName()
    if build_accelerator.Use():
      logging.info('Using %s build accelerator.', name)
      return name
    else:
      logging.info('Not using %s build accelerator.', name)
      return ''

  def GetBuildFormat(self):
    """Returns the desired build format."""
    return 'ninja'

  def GetName(self):
    """Returns the platform name."""
    return self._platform_name

  def GetDirectory(self):
    """Returns the directory of the platform configuration."""
    return self._directory

  # This happens post-construction to maintain compatibility with legacy
  # configurations.
  # TODO: Find some other way to do this.
  def SetDirectory(self, directory):
    """Sets the directory of the platform configuration."""
    try:
      i = self._application_configuration_search_path.index(self._directory)
      self._application_configuration_search_path.pop(i)
    except ValueError:
      i = 0
    self._directory = directory
    self._application_configuration_search_path.insert(i, self._directory)

  def AppendApplicationConfigurationPath(self, path):
    """Appends a path to search for ApplicationConfiguration modules."""
    self._application_configuration_search_path.append(path)

  def GetApplicationConfiguration(self, application_name):
    """Get the instance of ApplicationConfiguration for this app, if any.

    Looks for a class deriving from ApplicationConfiguration defined for
    this platform. If it finds one, this function will load, instantiate, and
    return the configuration. Otherwise, it will return None.

    Args:
      application_name: The name of the application to load, in a canonical
        filesystem-friendly form.

    Returns:
      An instance of ApplicationConfiguration defined for the application being
      loaded.
    """
    if not self._application_configuration:
      application_path = os.path.join(self.GetDirectory(), application_name)
      for directory in self._application_configuration_search_path:
        configuration_path = os.path.join(directory, application_name)
        logging.debug('Searching for ApplicationConfiguration in %s',
                      configuration_path)
        configuration_class = _GetApplicationConfigurationClass(
            configuration_path)
        if configuration_class:
          logging.info(
              'Using platform-specific ApplicationConfiguration for '
              '%s.', application_name)
          break

      if not configuration_class:
        configuration_class = environment.GetApplicationClass(application_name)
        if configuration_class:
          logging.info('Using default ApplicationConfiguration for %s.',
                       application_name)

      if not configuration_class:
        logging.info('Using base ApplicationConfiguration.')
        configuration_class = ApplicationConfiguration

      self._application_configuration = configuration_class(
          self, application_name, application_path)

    assert self._application_configuration
    return self._application_configuration

  def SetupPlatformTools(self, build_number):
    """Install tools, SDKs, etc.

    needed to build for the platform.

    Installs tools needed to build for this platform, possibly downloading
    and/or interacting with the user to do so. Raises a RuntimeError if the
    tools can't be installed or something is wrong that will prevent building.

    Args:
      build_number: The number identifying this build, generated at GYP time.
    """
    pass

  def GetIncludes(self):
    """Get a list of absolute paths to gypi files to include in order.

    These files will be included by GYP before any processed .gyp file. The
    application may add more gypi files to be processed before or after the
    files specified by this function.

    Returns:
      An ordered list containing absolute paths to .gypi files.
    """
    platform_info = platform.Get(self.GetName())
    if not platform_info:
      return []

    return [os.path.join(platform_info.path, 'gyp_configuration.gypi')]

  def GetEnvironmentVariables(self):
    """Get a Mapping of environment variable overrides.

    The environment variables returned by this function are set before calling
    GYP and can be used to manipulate the GYP behavior. They will override any
    environment variables of the same name in the calling environment, or any
    that are set by default. They may be overridden further by the application
    configuration.

    Returns:
      A dictionary containing environment variables.
    """
    return {}

  def GetVariables(self, config_name, use_clang=0):
    """Get a Mapping of GYP variable overrides.

    Get a Mapping of GYP variable names to values. Any defined values will
    override any values defined by default, but can themselves by overridden by
    the application configuration. GYP files can then have conditionals that
    check the values of these variables.

    Args:
      config_name: The name of the starboard.tools.config build type.
      use_clang: Whether the toolchain is a flavor of Clang.

    Raises:
      RuntimeError: When given conflicting configuration input.

    Returns:
      A Mapping of GYP variables to be merged into the global Mapping provided
      to GYP.
    """
    use_asan = 0
    use_tsan = 0
    use_source_code_coverage = 0

    if use_clang:
      # Enable source coverage instrumentation if USE_SOURCE_CODE_COVERAGE
      # was set to 1 in the environment.
      use_source_code_coverage = int(
          os.environ.get('USE_SOURCE_CODE_COVERAGE', 0))

      # Enable TSAN if USE_TSAN was set to 1 in the environment, unless
      # use_source_code_coverage is set.
      use_tsan = int(os.environ.get('USE_TSAN',
                                    0)) if not use_source_code_coverage else 0

      # Enable ASAN by default for debug and devel builds only if neither
      # use_tsan nor use_source_code_coverage is set.
      use_asan_default = self._asan_default if (
          not use_tsan and not use_source_code_coverage and
          config_name in (Config.DEBUG, Config.DEVEL)) else 0
      use_asan = int(os.environ.get('USE_ASAN', use_asan_default))
    if use_asan == 1 and use_tsan == 1:
      raise RuntimeError('ASAN and TSAN are mutually exclusive')

    if use_source_code_coverage:
      logging.info('Using Source-Based Code Coverage')

    if use_asan:
      logging.info('Using Address Sanitizer')

    if use_tsan:
      logging.info('Using Thread Sanitizer')

    variables = {
        'clang':
            use_clang,

        # Whether to build with clang's Source Based Code Coverage
        # instrumentation.
        # See https://clang.llvm.org/docs/SourceBasedCodeCoverage.html
        'use_source_code_coverage':
            use_source_code_coverage,

        # Whether to build with clang's Address Sanitizer instrumentation.
        'use_asan':
            use_asan,
        # Whether to build with clang's Thread Sanitizer instrumentation.
        'use_tsan':
            use_tsan,

        # If the path to the Starboard ABI file is able to be formatted, it will
        # be using the experimental Starboard API version of the file, i.e.
        # |sabi.SB_API_VERSION|. Otherwise, the value provided is just assumed
        # to be a complete path to a Starboard ABI file.
        'sabi_json_path':
            self.GetPathToSabiJsonFile().format(
                sb_api_version=sabi.SB_API_VERSION),

        # TODO: Remove these compatibility variables.
        'cobalt_config':
            config_name,
        'cobalt_fastbuild':
            0,
        'enable_vr':
            0,
    }
    return variables

  def GetGeneratorVariables(self, config_name):
    """Get a Mapping of generator variable overrides.

    Args:
      config_name: The name of the starboard.tools.config build type.

    Returns:
      A Mapping of generator variable names and values.
    """
    del config_name
    return {}

  def GetToolchain(self):
    """Returns the instance of the toolchain implementation class."""
    return None

  def GetTargetToolchain(self, **kwargs):
    """Returns a list of target tools."""
    # TODO: If this method throws |NotImplementedError|, GYP will fall back to
    #       the legacy toolchain. Once all platforms are migrated to the
    #       abstract toolchain, this method should be made |@abstractmethod|.
    raise NotImplementedError()

  def GetHostToolchain(self, **kwargs):
    """Returns a list of host tools."""
    # TODO: If this method throws |NotImplementedError|, GYP will fall back to
    #       the legacy toolchain. Once all platforms are migrated to the
    #       abstract toolchain, this method should be made |@abstractmethod|.
    raise NotImplementedError()

  def GetLauncherPath(self):
    """Gets the path to the launcher module for this platform."""
    return self.GetDirectory()

  def GetLauncher(self):
    """Gets the module used to launch applications on this platform."""
    module_path = os.path.abspath(
        os.path.join(self.GetLauncherPath(), 'launcher.py'))
    try:
      return imp.load_source('launcher', module_path)
    except (IOError, ImportError, RuntimeError) as error:
      logging.error('Unable to load launcher module from %s.', module_path)
      logging.error(error)
      return None

  def GetTestEnvVariables(self):
    """Gets a dict of environment variables needed by unit test binaries."""
    return {}

  def GetTestFilters(self):
    """Gets all tests to be excluded from a unit test run.

    Returns:
      A list of initialized starboard.tools.testing.TestFilter objects.
    """
    return []

  def GetDeployPathPatterns(self):
    """Gets deployment paths patterns for files to be included for deployment.

    Example: ['deploy/*.exe', 'content/*']

    Returns:
      A list of path wildcard patterns within the PRODUCT_DIR
      (src/out/<PLATFORM>_<CONFIG>) that need to be deployed in order for the
      platform launcher to run target executable(s).
    """
    raise NotImplementedError()

  def GetPathToSabiJsonFile(self):
    """Gets the path to the JSON file with Starboard ABI information for the
       build.

    Examples:
        'starboard/sabi/arm64/sabi-v12.json'
        'starboard/sabi/arm64/sabi-v{sb_api_version}.json'

    Returns:
      A string path to the appropriate Starboard ABI JSON file. This path can
      either be a complete path, or be capable of being formatted with an
      integer representing the desired Starboard API version. This file is
      required for a variety of definitions and variables pertaining to the ABI.
    """
    return 'starboard/sabi/default/sabi.json'

  def GetTestTargets(self):
    """Gets all tests to be run in a unit test run.

    Returns:
      A list of strings of test target names.
    """
    tests = [
        'app_key_files_test',
        'app_key_test',
        'drain_file_test',
        'elf_loader_test',
        'installation_manager_test',
        'nplb',
        'nplb_blitter_pixel_tests',
        'player_filter_tests',
        'slot_management_test',
        'starboard_platform_tests',
    ]
    tests.extend(get_optional_tests.GetOptionalTestTargets())
    return tests

  def GetDefaultTargetBuildFile(self):
    """Gets the build file to build by default."""
    return os.path.join(paths.STARBOARD_ROOT, 'starboard_all.gyp')
