# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(read-buffer) begin
(read-buffer) open "sample.txt"
(read-buffer) end
read-buffer: exit(0)
EOF
pass;
