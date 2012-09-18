@echo off
make
php ..\IPL_SDK\fix_size.php pspboot.bin
copy pspboot.bin h:\ipl\ipl.bin /y