ct;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(my-test-2) begin
(my-test-2) create "deleteme"
(my-test-2) open "deleteme"
(my-test-2) write "deleteme"
(my-test-2) tell "deleteme"
(my-test-2) read "deleteme"
(my-test-2) end
EOF
pass;
