@echo off
setlocal

:: --- 設定 ---
:: 客戶端程式的路徑 (請根據你的實際情況調整)
set CLIENT_EXE=.\build\Debug\client-app.exe

:: 要傳送的訊息
set MESSAGE="SimpleStressTest"

:: 要模擬的併發客戶端數量
set CONCURRENT_CLIENTS=200

:: --- 執行 ---
echo Starting %CONCURRENT_CLIENTS% concurrent clients...

:: 迴圈啟動客戶端
:: "start" 指令會讓每個客戶端在一個新的處理程序中非同步執行，而不會等待它結束
for /L %%i in (1, 1, %CONCURRENT_CLIENTS%) do (
    start "Client %%i" %CLIENT_EXE% %MESSAGE%
)

echo All clients have been launched.
endlocal