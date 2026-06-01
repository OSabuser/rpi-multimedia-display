#!/usr/bin/env bash
# git_ops.sh — операции по реструктуризации репозитория
# Запускать из корня репозитория

set -euo pipefail

echo "=== Реструктуризация deploy/ ==="

# 1. Создать deploy/systemd/ и переместить service/target файлы
#mkdir -p deploy/systemd
#git mv deploy/indicator.service    deploy/systemd/
#git mv deploy/indicator.target     deploy/systemd/
#git mv deploy/media-ingest.service deploy/systemd/
#git mv deploy/MUp-rpi0             deploy/systemd/
#echo "✅ systemd units → deploy/systemd/"

# 2. Создать deploy/configs/ и наполнить
#mkdir -p deploy/configs

# Из build-env/configs/device/ (video.toml, renderer.toml, nku_scheme.toml)
#git mv build-env/configs/device/video.toml    deploy/configs/
#git mv build-env/configs/device/renderer.toml deploy/configs/
#git mv build-env/configs/device/nku_scheme.toml deploy/configs/  # дефолт/шаблон
#echo "✅ device configs → deploy/configs/"

# Из git submodule (copy, не mv — submodule трогать нельзя)
#cp third_party/config_toolset/pi_nku_configs/menu_style.toml deploy/configs/
#cp third_party/config_toolset/pi_nku_configs/pi_scheme.toml  deploy/configs/
#git add deploy/configs/menu_style.toml deploy/configs/pi_scheme.toml
#echo "✅ menu_style.toml, pi_scheme.toml → deploy/configs/"

# 3. Создать deploy/resources/ из build-env/resources/ (без sounds/)
#mkdir -p deploy/resources/notifications  # пустая, Фаза 7

#git mv build-env/resources/BACK.png  deploy/resources/
#git mv build-env/resources/chars     deploy/resources/
#git mv build-env/resources/arrows    deploy/resources/
#git mv build-env/resources/modes     deploy/resources/
#git mv build-env/resources/weights   deploy/resources/
# promos/ и RULES.md — справочные материалы, не деплоятся
#git mv build-env/resources/promos    deploy/resources/   # или docs/ по желанию
#git mv build-env/resources/RULES.md  deploy/resources/
#echo "✅ PNG resources → deploy/resources/"

# 4. Создать deploy/sounds/ из build-env/resources/sounds/
#mkdir -p deploy/sounds
#git mv build-env/resources/sounds/* deploy/sounds/
# Удалить пустую папку sounds внутри resources
#git rm -r build-env/resources/sounds
echo "✅ WAV sounds → deploy/sounds/"

# 5. deploy/boot/config.txt
#mkdir -p deploy/boot
#git mv build-env/config.txt deploy/boot/config.txt
echo "✅ config.txt → deploy/boot/"

# 6. Убрать тестовый артефакт
#if [ -f build-env/output.mp4 ]; then
#    git rm build-env/output.mp4
#    echo "✅ build-env/output.mp4 удалён"
#fi

# 7. Убрать опустевший build-env/configs/
#if [ -d build-env/configs ]; then
#    git rm -r build-env/configs
#    echo "✅ build-env/configs/ удалён"
#fi

echo ""
echo "=== Итоговая структура deploy/ ==="
find deploy -type f | sort

echo ""
echo "  Не забыть:"
echo "  1. Обновить deploy/configs/renderer.toml → resources_dir = /data/resources"
echo "  2. Применить новые just/pi.just, scripts/smoke_test.sh, scripts/setup_pi.sh"
echo "  3. git commit -m 'refactor: reorganize deploy/ structure'"
