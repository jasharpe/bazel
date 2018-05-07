// Copyright 2017 The Bazel Authors. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <windows.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "src/tools/launcher/launcher.h"
#include "src/tools/launcher/util/data_parser.h"
#include "src/tools/launcher/util/launcher_util.h"
#include "src/main/cpp/util/file_platform.h"

namespace bazel {
namespace launcher {

using std::ifstream;
using std::ostringstream;
using std::string;
using std::unordered_map;
using std::vector;

static std::string GetRunfilesDir(const char* argv0) {
  string runfiles_dir;
  // If RUNFILES_DIR is already set (probably we are either in a test or in a
  // data dependency) then use it.
  if (GetEnv("RUNFILES_DIR", &runfiles_dir)) {
    return runfiles_dir;
  }
  // Otherwise this is probably a top-level non-test binary (e.g. a genrule
  // tool) and should look for its runfiles beside the executable.
  return GetBinaryPathWithExtension(argv0) + ".runfiles";
}

BinaryLauncherBase::BinaryLauncherBase(
    const LaunchDataParser::LaunchInfo& _launch_info, int argc, char* argv[])
    : launch_info(_launch_info),
      manifest_file(FindManifestFile(argv[0])),
      runfiles_dir(GetRunfilesDir(argv[0])),
      workspace_name(GetLaunchInfoByKey(WORKSPACE_NAME)) {
  for (int i = 0; i < argc; i++) {
    commandline_arguments.push_back(argv[i]);
  }
  // Prefer to use the runfiles manifest, if it exists, but otherwise the
  // runfiles directory will be used by default. On Windows, the manifest is
  // used locally, and the runfiles directory is used remotely.
  if (manifest_file != "") {
    ParseManifestFile(&manifest_file_map, manifest_file);
  }
}

static bool FindManifestFileImpl(const char* argv0, string* result) {
  // If this binary X runs as the data-dependency of some other binary Y, then
  // X has no runfiles manifest/directory and should use Y's.
  if (GetEnv("RUNFILES_MANIFEST_FILE", result) &&
      DoesFilePathExist(result->c_str())) {
    return true;
  }

  string directory;
  if (GetEnv("RUNFILES_DIR", &directory)) {
    *result = directory + "/MANIFEST";
    if (DoesFilePathExist(result->c_str())) {
      return true;
    }
  }

  // If this binary X runs by itself (not as a data-dependency of another
  // binary), then look for the manifest in a runfiles directory next to the
  // main binary, then look for it (the manifest) next to the main binary.
  directory = GetBinaryPathWithExtension(argv0) + ".runfiles";
  *result = directory + "/MANIFEST";
  if (DoesFilePathExist(result->c_str())) {
    return true;
  }

  *result = directory + "_manifest";
  if (DoesFilePathExist(result->c_str())) {
    return true;
  }

  return false;
}

string BinaryLauncherBase::FindManifestFile(const char* argv0) {
  string manifest_file;
  if (!FindManifestFileImpl(argv0, &manifest_file)) {
    return "";
  }
  // The path will be set as the RUNFILES_MANIFEST_FILE envvar and used by the
  // shell script, so let's convert backslashes to forward slashes.
  std::replace(manifest_file.begin(), manifest_file.end(), '\\', '/');
  return manifest_file;
}

string BinaryLauncherBase::GetRunfilesPath() const {
  string runfiles_path =
      GetBinaryPathWithExtension(this->commandline_arguments[0]) + ".runfiles";
  std::replace(runfiles_path.begin(), runfiles_path.end(), '/', '\\');
  return runfiles_path;
}

void BinaryLauncherBase::ParseManifestFile(ManifestFileMap* manifest_file_map,
                                           const string& manifest_path) {
  ifstream manifest_file(AsAbsoluteWindowsPath(manifest_path.c_str()).c_str());

  if (!manifest_file) {
    die("Couldn't open MANIFEST file: %s", manifest_path.c_str());
  }

  string line;
  while (getline(manifest_file, line)) {
    size_t space_pos = line.find_first_of(' ');
    if (space_pos == string::npos) {
      die("Wrong MANIFEST format at line: %s", line.c_str());
    }
    string key = line.substr(0, space_pos);
    string value = line.substr(space_pos + 1);
    manifest_file_map->insert(make_pair(key, value));
  }
}

string BinaryLauncherBase::Rlocation(const string& path,
                                     bool need_workspace_name) const {
  // If the manifest file map is empty, then we're using the runfiles directory
  // instead.
  if (manifest_file_map.empty()) {
    if (blaze_util::IsAbsolute(path)) {
      return path;
    }

    string query_path = runfiles_dir;
    if (need_workspace_name) {
      query_path += "/" + this->workspace_name;
    }
    query_path += "/" + path;
    return query_path;
  }

  string query_path = path;
  if (need_workspace_name) {
    query_path = this->workspace_name + "/" + path;
  }
  auto entry = manifest_file_map.find(query_path);
  if (entry == manifest_file_map.end()) {
    die("Rlocation failed on %s, path doesn't exist in MANIFEST file",
        query_path.c_str());
  }
  return entry->second;
}

string BinaryLauncherBase::GetLaunchInfoByKey(const string& key) {
  auto item = launch_info.find(key);
  if (item == launch_info.end()) {
    die("Cannot find key \"%s\" from launch data.\n", key.c_str());
  }
  return item->second;
}

const vector<string>& BinaryLauncherBase::GetCommandlineArguments() const {
  return this->commandline_arguments;
}

void BinaryLauncherBase::CreateCommandLine(
    CmdLine* result, const string& executable,
    const vector<string>& arguments) const {
  ostringstream cmdline;
  cmdline << '\"' << executable << '\"';
  for (const auto& s : arguments) {
    cmdline << ' ' << s;
  }

  string cmdline_str = cmdline.str();
  if (cmdline_str.size() >= MAX_CMDLINE_LENGTH) {
    die("Command line too long: %s", cmdline_str.c_str());
  }

  // Copy command line into a mutable buffer.
  // CreateProcess is allowed to mutate its command line argument.
  strncpy(result->cmdline, cmdline_str.c_str(), MAX_CMDLINE_LENGTH - 1);
  result->cmdline[MAX_CMDLINE_LENGTH - 1] = 0;
}

bool BinaryLauncherBase::PrintLauncherCommandLine(
    const string& executable, const vector<string>& arguments) const {
  bool has_print_cmd_flag = false;
  for (const auto& arg : arguments) {
    has_print_cmd_flag |= (arg == "--print_launcher_command");
  }
  if (has_print_cmd_flag) {
    printf("%s\n", executable.c_str());
    for (const auto& arg : arguments) {
      printf("%s\n", arg.c_str());
    }
  }
  return has_print_cmd_flag;
}

ExitCode BinaryLauncherBase::LaunchProcess(const string& executable,
                                           const vector<string>& arguments,
                                           bool suppressOutput) const {
  if (PrintLauncherCommandLine(executable, arguments)) {
    return 0;
  }
  if (manifest_file != "") {
    SetEnv("RUNFILES_MANIFEST_ONLY", "1");
    SetEnv("RUNFILES_MANIFEST_FILE", manifest_file);
  } else {
    SetEnv("RUNFILES_DIR", runfiles_dir);
  }
  CmdLine cmdline;
  CreateCommandLine(&cmdline, executable, arguments);
  PROCESS_INFORMATION processInfo = {0};
  STARTUPINFOA startupInfo = {0};
  startupInfo.cb = sizeof(startupInfo);
  BOOL ok = CreateProcessA(
      /* lpApplicationName */ NULL,
      /* lpCommandLine */ cmdline.cmdline,
      /* lpProcessAttributes */ NULL,
      /* lpThreadAttributes */ NULL,
      /* bInheritHandles */ FALSE,
      /* dwCreationFlags */
          suppressOutput ? CREATE_NO_WINDOW  // no console window => no output
                         : 0,
      /* lpEnvironment */ NULL,
      /* lpCurrentDirectory */ NULL,
      /* lpStartupInfo */ &startupInfo,
      /* lpProcessInformation */ &processInfo);
  if (!ok) {
    PrintError("Cannot launch process: %s\nReason: %s",
               cmdline.cmdline,
               GetLastErrorString().c_str());
    return GetLastError();
  }
  WaitForSingleObject(processInfo.hProcess, INFINITE);
  ExitCode exit_code;
  GetExitCodeProcess(processInfo.hProcess,
                     reinterpret_cast<LPDWORD>(&exit_code));
  CloseHandle(processInfo.hProcess);
  CloseHandle(processInfo.hThread);
  return exit_code;
}

}  // namespace launcher
}  // namespace bazel
