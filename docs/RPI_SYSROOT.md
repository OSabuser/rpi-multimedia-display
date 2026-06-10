# Как в проекте создаётся sysroot для Raspberry Pi Zero 2W и как он работает при сборке

## Что такое sysroot в этом проекте

В данном проекте sysroot — это локальная копия ключевых частей файловой системы целевой Raspberry Pi Zero 2W: системных библиотек, заголовков и каталога `/opt/vc`, который содержит специфичные для Raspberry Pi библиотеки и заголовки графического стека Broadcom.

Смысл такого sysroot в том, чтобы кросс-компилятор на хосте или в Docker собирал программу не против библиотек своей собственной среды, а против тех ABI, заголовков и `.so`, которые реально установлены на целевой Pi.

Это особенно важно в данном случае, потому что toolchain может находиться в более новой среде Bookworm, тогда как сама Raspberry Pi использует Buster с glibc 2.28. Без sysroot линковка могла бы случайно пройти против более новой glibc хоста, а готовый бинарник потом не запустился бы на целевом устройстве.

## Как sysroot создаётся

Sysroot формируется правилом `fetch-sysroot`, которое по `rsync` подключается к живой Raspberry Pi и копирует оттуда нужные каталоги в локальную директорию `build-env/pi-sysroot`.

Используются такие команды:

```make
rsync -av pi@${PI_HOST}:/opt/vc/           build-env/pi-sysroot/opt/vc/
rsync -av pi@${PI_HOST}:/lib/arm-linux-gnueabihf/         build-env/pi-sysroot/lib/arm-linux-gnueabihf/
rsync -av pi@${PI_HOST}:/usr/lib/arm-linux-gnueabihf/     build-env/pi-sysroot/usr/lib/arm-linux-gnueabihf/
rsync -av pi@${PI_HOST}:/usr/include/                     build-env/pi-sysroot/usr/include/
rsync -av pi@${PI_HOST}:/usr/include/arm-linux-gnueabihf/ build-env/pi-sysroot/usr/include/arm-linux-gnueabihf/
```

Каждая строка копирует отдельную часть runtime и SDK целевой системы:

- `/opt/vc/` — проприетарные библиотеки и заголовки Raspberry Pi для `bcm_host`, DispmanX, EGL/GLES и связанного видеостека Broadcom.
- `/lib/arm-linux-gnueabihf/` — базовые системные библиотеки runtime, включая glibc и динамический загрузчик под ARM hard-float ABI.
- `/usr/lib/arm-linux-gnueabihf/` — пользовательские ARM-библиотеки, поставляемые пакетами системы.
- `/usr/include/` — общие системные заголовки, используемые компилятором при сборке.
- `/usr/include/arm-linux-gnueabihf/` — multiarch-специфичные заголовки Debian/Raspbian-окружения для ARM hard-float.

В результате `build-env/pi-sysroot` становится локальным снимком тех частей root filesystem, которые нужны для сборки и линковки под реальную Pi Zero 2W.

## Почему копируются именно эти каталоги

Для кросс-компиляции под Linux мало иметь один только `arm-linux-gnueabihf-gcc`. Нужны ещё заголовки и библиотеки именно той системы, на которой будет запускаться приложение.

Каталоги `/lib/arm-linux-gnueabihf` и `/usr/lib/arm-linux-gnueabihf` обеспечивают совместимость на уровне ABI и версии runtime, а `/usr/include` и `/usr/include/arm-linux-gnueabihf` дают компилятору правильные объявления функций, типов и структур для той же самой пользовательской среды.

Каталог `/opt/vc` нужен отдельно, потому что исторически Raspberry Pi размещала там собственный графический стек и заголовки, которые не лежат в стандартных системных путях `/usr/include` и `/usr/lib`.

## Как этот sysroot подключается в CMake

В toolchain-файле используется такая конфигурация:

```cmake
set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CROSS_TRIPLE "arm-linux-gnueabihf")

set(CMAKE_C_COMPILER   "${CROSS_TRIPLE}-gcc")
set(CMAKE_CXX_COMPILER "${CROSS_TRIPLE}-g++")
set(CMAKE_STRIP        "${CROSS_TRIPLE}-strip")

set(CMAKE_SYSROOT "/pi-sysroot")
set(CMAKE_FIND_ROOT_PATH "/pi-sysroot")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
```

`CMAKE_SYSTEM_NAME` и `CMAKE_SYSTEM_PROCESSOR` переводят CMake в режим кросс-компиляции под Linux/ARM, а выбор `arm-linux-gnueabihf-gcc` и `arm-linux-gnueabihf-g++` задаёт нужный кросс-тулчейн.

`CMAKE_SYSROOT` — ключевая настройка: она заставляет CMake передавать компилятору и линкеру `--sysroot=/pi-sysroot`, то есть использовать `/pi-sysroot` как логический корень файловой системы таргета.

Из-за этого стандартные системные пути при сборке интерпретируются не относительно контейнера Bookworm, а относительно содержимого sysroot. Проще говоря, `/usr/include` означает `/pi-sysroot/usr/include`, а `/usr/lib` означает `/pi-sysroot/usr/lib`.

`CMAKE_FIND_ROOT_PATH` и режимы `ONLY` для include/library нужны дополнительно для `find_library`, `find_path` и `find_package`, чтобы CMake не подцепил случайно библиотеки и заголовки хостовой системы вместо ARM-версий из sysroot.[cite:20]

При этом `CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER` специально оставляет поиск исполняемых build-утилит в среде контейнера, потому что такие программы, как `pkg-config`, `cmake`, `python`, `wayland-scanner` или генераторы кода, должны выполняться на хосте сборки, а не браться из ARM sysroot.[cite:20]

## Как это работает во время сборки

Во время компиляции запускается кросс-компилятор `arm-linux-gnueabihf-gcc`, но все системные include и библиотеки он ищет внутри `/pi-sysroot`, а не в обычной файловой системе контейнера.

Это означает, что исходный код компилируется с заголовками целевой Raspberry Pi, а линковка выполняется с библиотеками и glibc той же версии, что установлены на самой Zero 2W.

Если в контейнере установлен более новый runtime, он не должен участвовать в линковке целевого приложения, потому что благодаря `CMAKE_SYSROOT` и `CMAKE_FIND_ROOT_PATH` приоритет полностью отдан Buster sysroot.

Именно поэтому описанная в комментарии проблема с glibc 2.34+ из Bookworm решается: линкер видит `libc.so.6` и прочие зависимости из `/pi-sysroot/lib/arm-linux-gnueabihf` и `/pi-sysroot/usr/lib/arm-linux-gnueabihf`, то есть из Buster-окружения.

## Что происходит с /opt/vc при сборке

В toolchain-файле отдельно задаётся путь к Raspberry Pi VideoCore SDK:

```cmake
set(VC_DIR "${CMAKE_SYSROOT}/opt/vc")

include_directories(SYSTEM
    "${VC_DIR}/include"
    "${VC_DIR}/include/interface/vcos/pthreads"
    "${VC_DIR}/include/interface/vmcs_host/linux"
)

link_directories("${VC_DIR}/lib")
```

`VC_DIR` ссылается на `/pi-sysroot/opt/vc`, то есть на копию каталога `/opt/vc`, забранную с целевой Pi.

Через `include_directories(SYSTEM ...)` компилятор получает доступ к заголовкам `bcm_host`, VCOS и другим интерфейсам Broadcom, а через `link_directories(...)` линкер начинает видеть соответствующие библиотеки в `opt/vc/lib`.

Поэтому проект может собираться против DispmanX, `bcm_host`, EGL или GLES, даже если сам build идёт не на малине, а в x86_64 Docker-контейнере.

## Почему заданы именно такие флаги CPU/FPU

В toolchain-файле используется строка:

```cmake
set(CMAKE_C_FLAGS_INIT "-march=armv8-a -mtune=cortex-a53 -mfpu=neon-fp-armv8 -mfloat-abi=hard")
```

Raspberry Pi Zero 2W построена на Cortex-A53, то есть на ARMv8-A ядрах, но в данном сценарии используется 32-битный ABI `arm-linux-gnueabihf`.

Поэтому сочетание `-march=armv8-a`, `-mtune=cortex-a53`, `-mfpu=neon-fp-armv8` и `-mfloat-abi=hard` означает: генерировать 32-битный ARM-код, оптимизированный под A53, с использованием hard-float ABI и аппаратного FPU/NEON, совместимых с этим таргетом.

Эти флаги должны совпадать и с архитектурой CPU, и с ABI библиотек внутри sysroot, иначе можно получить несовместимость на уровне вызова функций или формата бинарных зависимостей.

## Полная картина процесса

Ниже весь процесс в логическом порядке:

1. На живой Raspberry Pi Zero 2W установлены нужные системные пакеты и библиотеки.
2. Команда `fetch-sysroot` через `rsync` копирует с Pi системные include, ARM-библиотеки и `/opt/vc` в каталог `build-env/pi-sysroot`.
3. Этот каталог пробрасывается в Docker-контейнер как `/pi-sysroot`.[cite:16]
4. CMake toolchain-файл объявляет `/pi-sysroot` как `CMAKE_SYSROOT` и как корень поиска include/library для `find_*` команд.
5. Кросс-компилятор `arm-linux-gnueabihf-gcc` собирает ARM-исполняемый файл, используя заголовки и библиотеки из Buster sysroot, а не из среды контейнера.
6. Готовый бинарник оказывается совместимым с Raspberry Pi Zero 2W, потому что был собран против того же userspace, который реально присутствует на устройстве.

