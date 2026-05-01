@echo off
set IDF_PATH=D:\program\idf\v5.4\esp-idf
set IDF_PYTHON=C:\Users\reddy\.espressif\python_env\idf5.4_py3.11_env\Scripts\python.exe
set PATH=C:\Users\reddy\.espressif\python_env\idf5.4_py3.11_env\Scripts;%IDF_PATH%\tools
set PATH=C:\Users\reddy\.espressif\tools\cmake\3.30.2\bin;%PATH%
set PATH=C:\Users\reddy\.espressif\tools\ninja\1.12.1;%PATH%
set PATH=C:\Users\reddy\.espressif\tools\xtensa-esp-elf\esp-14.2.0_20241119\xtensa-esp-elf\bin;%PATH%
set PATH=C:\Users\reddy\.espressif\tools\riscv32-esp-elf\esp-14.2.0_20241119\riscv32-esp-elf\bin;%PATH%
set PATH=C:\Users\reddy\.espressif\tools\esp-rom-elfs\20241011;%PATH%
set PATH=C:\Users\reddy\.espressif\tools\idf-git\2.39.2\cmd;%PATH%
set MSYSTEM=

cd /d D:\ESP32-S3-Touch-AMOLED-1.8-main

echo === Building ===
python "%IDF_PATH%\tools\idf.py" build
if %ERRORLEVEL% NEQ 0 (
    echo BUILD FAILED
    pause
    exit /b 1
)

echo.
echo === Flashing to COM9 ===
python "%IDF_PATH%\tools\idf.py" flash -p COM9
if %ERRORLEVEL% NEQ 0 (
    echo FLASH FAILED
    pause
    exit /b 1
)

echo.
echo === Done! ===
pause
