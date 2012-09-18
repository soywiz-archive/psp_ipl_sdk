<?php

$file = &$argv[1];

if (!isset($file)) {
	die("Must specify a IPL BIN file to fix\n");
}

if (!file_exists($file)) {
	die("File '$file' doesn't exist\n");
}

$size = filesize($file);
$f = fopen($file, 'r+b');
fseek($f, 4);
fwrite($f, pack('V', $size - 0x10));
fclose($f);