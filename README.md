requirement: 
  vcpkg、asio、spdlog、GTest

usage: 
  server: 
  .\build\Debug\server-app.exe
  
  client: 
   .\build\Debug\client-app.exe <並行數> <測試時間(s)> <每次休息時間(ms)> <傳送訊息>
  EX: .\build\Debug\client-app.exe 5000 10 50 "Hello Server!"

  
