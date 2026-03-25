@echo off
g++ -E C:/Qt/6.9.3/mingw_64/mkspecs/features/data/macros.cpp 2>NUL
echo EXITCODE=%ERRORLEVEL%