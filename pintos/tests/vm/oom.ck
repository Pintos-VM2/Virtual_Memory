# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_USER_FAULTS => 1, IGNORE_EXIT_CODES => 1, [<<'EOF']);
(oom) begin
(oom) Spawned at least 10 children.
(oom) success. Program forked 10 iterations.
(oom) end
EOF
pass;
