# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF', <<'EOF']);
(write-buffer) begin
(write-buffer) create "test.txt"
(write-buffer) open "test.txt"
(write-buffer) end
write-buffer: exit(0)
EOF
(write-buffer) begin
(write-buffer) create "test.txt"
(write-buffer) open "test.txt"
write-buffer: exit(-1)
EOF
pass;
