# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(my-test-1) begin
(my-test-1) create "deleteme"
(my-test-1) open "deleteme"
(my-test-1) write "deleteme"
(my-test-1) tell "deleteme"
(my-test-1) seek "deleteme" to 0
(my-test-1) tell "deleteme"
(my-test-1) seek "deleteme" to 197
(my-test-1) tell "deleteme"
(my-test-1) close "deleteme"
(my-test-1) end
EOF
pass;