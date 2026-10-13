драйвер датчика (honeywall.c)
hw_init(i2cx)
hw_read -- возврат данных или ошибьку.

в главном цикле мигание светика, чтение данных с двух датчиков (если доступны).
вывод в юарт в виде
NAME ID yyyy:mm:dd hh:mm:ss.xxx hum1 t1  hum2 t2 cpu_temp [s1_ok s2_ok s1_received s2_received uart_received ]
вывод всегда раз в N сек.
[] -- debug вывод
NAME -- HMP556
ID по умолчанию 001, хранится во флеши

меню возьми как тут сделано C:\src\VNIIM\hmp555\fw\Core\Src
чтение и запись юарта DMA.
команды:
help -- вывод меню
info -- вывод инфы что это VNIIM Humidity sensor (HMP556) ver 0.1 build xxx commit xxx
set_id ID
set_br BAUDRATE
freq [0.01--100 Hz] частота выдачи в порт.

для флеши возьми мой драйвер тут C:\src\VNIIM\hmp555\fw\Core\Src
