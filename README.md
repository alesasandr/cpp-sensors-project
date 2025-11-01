🛰️ cpp-sensors-project

cpp-sensors-project — это минималистичный высокопроизводительный сервер на C++17
для приёма телеметрии от датчиков и записи данных в ClickHouse.
Проект рассчитан на лёгкий деплой под Windows 11 + WSL2, поддерживает нативный протокол ClickHouse (порт 9000) и полностью асинхронен.

🚀 Возможности

Приём JSON-данных с датчиков через HTTP (/ingest)

Асинхронная очередь и пул соединений к ClickHouse (clickhouse-cpp)

Поддержка параллельных HTTP-воркеров

Автоматическая конвертация timestamp из сек/мс/мкс

JSON-ответы с кодами статусов

Простая конфигурация через config/server.json

Кроссплатформенная сборка (Windows, Linux, WSL)

🧩 Пример JSON-запроса
{
  "sensor_id": "dev-1",
  "ts": 1730460000,
  "metrics": {
    "temperature": 21.5,
    "humidity": 45.2
  }
}


Ответ:

{"status": "ok"}

🗄️ Таблица в ClickHouse
CREATE TABLE sensors.metrics (
  sensor_id String,
  ts DateTime,
  key String,
  value Float64
)
ENGINE = MergeTree
ORDER BY (sensor_id, ts);

⚙️ Конфигурация config/server.json
{
  "host": "0.0.0.0",
  "port": 8080,
  "http_threads": 4,
  "ch_pool_size": 8,
  "queue_capacity": 200000,
  "write_timeout_ms": 3000,
  "ch_host": "127.0.0.1",
  "ch_port": 9000,
  "ch_user": "default",
  "ch_password": "chpass",
  "ch_database": "sensors",
  "ch_table": "metrics"
}

💻 Быстрый старт
1️⃣ Запусти ClickHouse в Docker
docker run -d --name ch -p 9000:9000 -p 8123:8123 \
  -e CLICKHOUSE_PASSWORD=chpass clickhouse/clickhouse-server

2️⃣ Собери проект
cmake --preset msvc
cmake --build out/build/msvc --config Release

3️⃣ Запусти сервер
out\build\msvc\Release\cpp-sensors-project.exe

4️⃣ Отправь тестовый пакет
$ts = [int][double]::Parse((Get-Date -Date (Get-Date).ToUniversalTime() -UFormat %s))
$body = @{
  sensor_id = "dev-1"
  ts        = $ts
  metrics   = @{ temperature = 21.5; humidity = 45.2 }
} | ConvertTo-Json -Depth 5

Invoke-RestMethod -Uri "http://127.0.0.1:8080/ingest" `
  -Method Post -ContentType "application/json" -Body $body

5️⃣ Проверь в ClickHouse
docker exec -it ch clickhouse-client -u default --password chpass \
  -q "SELECT sensor_id, ts, key, value FROM sensors.metrics ORDER BY ts DESC LIMIT 5 FORMAT PrettyCompactMonoBlock"

🧠 Архитектура
        +-----------+        +--------------------+
        | Sensors   |  -->   | cpp-sensors-project|
        |  (curl)   |        |  HTTP /ingest      |
        +-----------+        +---------+----------+
                                       |
                                       v
                           +----------------------+
                           |  ClickHouse (native) |
                           +----------------------+


HTTP-часть построена на Boost.Beast

Асинхронная очередь — thread-safe реализация на std::mutex + std::condition_variable

Пул потоков ClickHouse использует clickhouse::Client из официальной библиотеки

🧩 Директории проекта
Папка / файл	Назначение
src/	Исходники сервера
include/	Заголовочные файлы
config/	Конфиги и пример server.json.example
scripts/	Утилиты и тестовые генераторы
.vscode/	Настройки среды разработки
out/	Каталог сборки (игнорируется в git)
🧪 Самотест (опционально)

Можно собрать clickhouse_pool.cpp с флагом -DSENSORS_CH_SELFTEST,
чтобы выполнить автономную проверку вставки строки probe/selftest в ClickHouse:

cmake -DSENSORS_CH_SELFTEST=ON .
cmake --build . --config Release
ch_selftest 127.0.0.1 9000 default chpass sensors metrics

🧰 Требования

CMake ≥ 3.20

MSVC / Clang / GCC с поддержкой C++17

Boost.Beast ≥ 1.74

clickhouse-cpp (внешняя библиотека)
