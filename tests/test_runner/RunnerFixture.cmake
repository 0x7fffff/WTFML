# Synthetic fixture template; copied as CTestTestfile.cmake only in the run directory.
find_program(RUNNER_CMAKE NAMES cmake REQUIRED)
add_test(wtfml.core "${RUNNER_CMAKE}" -E true)
add_test(wtfml.mod-action-panel "${RUNNER_CMAKE}" -E false)
add_test(wtfml.java-manage "${RUNNER_CMAKE}" -E sleep 20)
