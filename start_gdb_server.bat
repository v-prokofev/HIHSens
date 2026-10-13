@echo off
title OpenOCD GDB Server for STM32F103 (Port 3333)
echo ========================================================
echo Starting OpenOCD GDB Server for STM32F103 (Port 3333)...
echo Supports ST-LINK clones and CS32/STM32 chips!
echo ========================================================

"C:\ST\STM32CubeIDE_1.9.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.openocd.win32_2.2.0.202202231230\tools\bin\openocd.exe" -f "%~dp0openocd.cfg"
pause
