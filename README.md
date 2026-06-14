# Kubik

Десктопное приложение для просмотра и обработки 3D post-stack кубов в формате SEG-Y.

## Возможности

### Просмотр куба

- Открытие файлов `.sgy` / `.segy` (меню **Файл → Открыть** или перетаскивание в окно)
- Навигация по срезам **Inline**, **Crossline** и **Time** (кнопки IL / XL / T, колесо мыши, навигатор)
- Горячие клавиши: `I` — Inline, `X` — Crossline, `T` — Time
- Палитры отображения (Grayscale, Viridis, Red/Blue, Petrel, Kingdom) и настройка clip
- Подсветка области обрезки на срезе

### Обрезка и ресэмплинг

- **Обрезка** — ограничение диапазона по IL, XL и времени; превью на срезах и применение при сохранении
- **Ресэмпинг** — изменение шага по времени (Δt) и пространственной сетке (Δ Inline, Δ Crossline)

### FFT-фильтрация (срезы IL / XL)

- 1D FFT-фильтр вдоль времени: bandpass, low-pass, high-pass, notch (Butterworth)
- Интерактивная настройка по выделенной области среза (спектр, превью до/после)
- **Apply To Cube** — сохранение параметров и превью фильтра на всех срезах IL/XL
- Применение при сохранении SEG-Y (если включён чекбокс «Фильтр включён»)

### Футпринты (срезы Time)

- 2D footprint-фильтр в домене k_IL × k_XL (режимы Footprint IL, XL, IL-XL)
- Интерактивная настройка с 2D-спектром и превью
- **Apply To Cube** — превью на всех Time-срезах и применение при сохранении

### Сохранение и информация

- **Сохранить как…** — экспорт с учётом обрезки, ресэмплинга и активных фильтров (выход в IEEE float, SEGY_BIN_FORMAT=5)
- Прогресс-бар с отображением этапов обработки и времени выполнения
- Просмотр текстовых и бинарных заголовков SEG-Y
- Координаты съёмки и карта CDP

## Требования для сборки

- **CMake** ≥ 3.18
- **Компилятор C++17, 64-bit** (GCC 7+, Clang, MSVC 2017+)
- **Qt 5** (модуль Widgets)
- **Git** — для загрузки зависимостей через CMake FetchContent
- **OpenMP** (опционально) — ускорение чтения срезов

Зависимости **segyio** и **KFR** (FFT) подтягиваются автоматически при конфигурации CMake.

### Ubuntu / Debian

```bash
sudo apt install build-essential cmake git qtbase5-dev
```

## Сборка

```bash
git clone <url-репозитория> cubetools
cd cubetools

cmake -S . -B build
cmake --build build -j$(nproc)
```

Исполняемый файл: `build/kubik`

Запуск:

```bash
./build/kubik
```

### Опции CMake

| Опция | По умолчанию | Описание |
|-------|--------------|----------|
| `KUBIK_ENABLE_OPENMP` | `ON` | OpenMP для параллельного чтения срезов |

Пример отключения OpenMP:

```bash
cmake -S . -B build -DKUBIK_ENABLE_OPENMP=OFF
```

### Windows (Qt Creator)

**32-bit MinGW (`mingw730_32`, `msys_pe_32bit`) не поддерживается** — библиотека KFR (FFT) на нём не собирается. Используйте **64-bit kit**:

| Вариант | Kit в Qt Creator | Что установить |
|---------|------------------|----------------|
| **MinGW 64-bit** (проще) | `Desktop Qt 5.15.2 MinGW 8.1 64-bit` | Qt 5.15.2 + компонент MinGW 8.1 64-bit |
| **MSVC 64-bit** (надёжнее для KFR) | `Desktop Qt 5.15.2 MSVC2019 64bit` | Visual Studio 2019/2022 (C++ workload) + Qt под MSVC |

Шаги:

1. **Edit → Preferences → Kits** — убедитесь, что есть 64-bit kit (не `32bit`, не `i686`).
2. **Projects → Build & Run** — выберите этот kit.
3. **Build → Clear CMake Configuration**, затем пересоберите.
4. При проблемах с OpenMP: `-DKUBIK_ENABLE_OPENMP=OFF` в аргументах CMake.

Если установлен только Qt 5.14.2 с `mingw730_32` — доустановите [Qt 5.15.2](https://www.qt.io/download-open-source) с **MinGW 8.1 64-bit** или поставьте MSVC + Qt под MSVC.

### Тесты

```bash
cd build
ctest
```

Тест `segy_open_test` проверяет открытие, обрезку и сохранение на примере из `data/both_stime_25x5.sgy` (файл должен быть в репозитории).

## Структура проекта

```
include/kubik/     — публичные заголовки (SegyCube, FftFilter, Resample)
src/segy/          — чтение/запись SEG-Y, ресэмплинг
src/dsp/           — FFT- и footprint-фильтры
src/gui/           — интерфейс Qt
tests/             — модульные тесты
```
