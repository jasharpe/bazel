#!/bin/bash
# Copyright 2015 The Bazel Authors. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# not a proper test suite, but
# - a sanity check that unittest.bash is syntactically valid
# - and a means to run some quick experiments

set -euo pipefail
# --- begin runfiles.bash initialization ---
if [[ ! -d "${RUNFILES_DIR:-/dev/null}" && ! -f "${RUNFILES_MANIFEST_FILE:-/dev/null}" ]]; then
    if [[ -f "$0.runfiles_manifest" ]]; then
      export RUNFILES_MANIFEST_FILE="$0.runfiles_manifest"
    elif [[ -f "$0.runfiles/MANIFEST" ]]; then
      export RUNFILES_MANIFEST_FILE="$0.runfiles/MANIFEST"
    elif [[ -f "$0.runfiles/io_bazel/tools/bash/runfiles/runfiles.bash" ]]; then
      export RUNFILES_DIR="$0.runfiles"
    elif [[ -f "$TEST_SRCDIR/io_bazel/tools/bash/runfiles/runfiles.bash" ]]; then
      export RUNFILES_DIR="$TEST_SRCDIR"
    fi
fi
if [[ -f "${RUNFILES_DIR:-/dev/null}/io_bazel/tools/bash/runfiles/runfiles.bash" ]]; then
  source "${RUNFILES_DIR}/io_bazel/tools/bash/runfiles/runfiles.bash"
elif [[ -f "${RUNFILES_MANIFEST_FILE:-/dev/null}" ]]; then
  source "$(grep -m1 "^io_bazel/tools/bash/runfiles/runfiles.bash " \
            "$RUNFILES_MANIFEST_FILE" | cut -d ' ' -f 2-)"
else
  echo >&2 "ERROR: cannot find //tools/bash/runfiles:runfiles.bash"
  exit 1
fi
# --- end runfiles.bash initialization ---

source "$(rlocation "io_bazel/src/test/shell/unittest.bash")" || { echo "Could not source unittest.bash" >&2; exit 1; }

function set_up() {
  tmp_TEST_TMPDIR=$TEST_TMPDIR
  TEST_TMPDIR=$TEST_TMPDIR/$TEST_name
  mkdir -p $TEST_TMPDIR
}

function tear_down() {
  TEST_TMPDIR=$tmp_TEST_TMPDIR
}

function test_1() {
  echo "Everything is okay in test_1"
}

function test_2() {
  echo "Everything is okay in test_2"
}

function test_timestamp() {
  local ts=$(timestamp)
  [[ $ts =~ ^[0-9]{13}$ ]] || fail "timestamp wan't valid: $ts"

  local time_diff=$(get_run_time 100000 223456)
  assert_equals $time_diff 123.456
}

function test_failure_message() {
  cd $TEST_TMPDIR
  cat > thing.sh <<EOF
#!/bin/bash
XML_OUTPUT_FILE=${TEST_TMPDIR}/dummy.xml
source "$(rlocation "io_bazel/src/test/shell/unittest.bash")"

function test_thing() {
  fail "I'm a failure with <>&\" escaped symbols"
}

run_suite "thing tests"
EOF
  chmod +x thing.sh
  ./thing.sh &> $TEST_log && fail "thing.sh should fail"
  expect_not_log "__fail: No such file or directory"
  assert_contains "message=\"I'm a failure with &lt;&gt;&amp;&quot; escaped symbols\"" ${TEST_TMPDIR}/dummy.xml
  assert_contains "I'm a failure with <>&\" escaped symbols" ${TEST_TMPDIR}/dummy.xml
  assert_contains 'errors="1"' ${TEST_TMPDIR}/dummy.xml
}

function test_no_failure_message() {
  cd $TEST_TMPDIR
  cat > thing.sh <<EOF
#!/bin/bash
XML_OUTPUT_FILE=${TEST_TMPDIR}/dummy.xml
source "$(rlocation "io_bazel/src/test/shell/unittest.bash")"

function test_thing() {
  TEST_passed=blorp
}

run_suite "thing tests"
EOF
  chmod +x thing.sh
  ./thing.sh &> $TEST_log && fail "thing.sh should fail"
  expect_not_log "__fail: No such file or directory"
  assert_contains "No failure message" ${TEST_TMPDIR}/dummy.xml
}

function test_errexit_prints_stack_trace() {
  cd $TEST_TMPDIR
  cat > thing.sh <<EOF
#!/bin/bash
XML_OUTPUT_FILE=${TEST_TMPDIR}/dummy.xml
source "$(rlocation "io_bazel/src/test/shell/unittest.bash")"

enable_errexit

function helper() {
  echo before
  false
  echo after
}

function test_thing() {
  helper
}

run_suite "thing tests"
EOF
  chmod +x thing.sh
  ./thing.sh &> $TEST_log && fail "thing.sh should fail"
  #cat $TEST_log

  # Make sure the full stack trace is there.
  expect_log "test_thing FAILED: terminated because this command returned a non-zero status:"
  expect_log "./thing.sh:[0-9]*: in call to helper"
  expect_log "./thing.sh:[0-9]*: in call to test_thing"
}

run_suite "unittests Tests"
