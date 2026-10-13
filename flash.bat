@echo off
"C:\ST\STM32CubeIDE_1.9.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_2.0.200.202202231230\tools\bin\STM32_Programmer_CLI.exe" -c port=SWD -w "Debug\hw.elf" -v -rst
pause
