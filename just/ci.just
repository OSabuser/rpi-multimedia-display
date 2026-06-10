# =============================================================================
# just/ci.just — CI/CD pipeline
# Выполняется в CI (GitHub Actions / GitLab CI).
#
# Stage 1 — без железа, только reproducible шаги:
#   - host unit-тесты
#   - форматирование
# Stage 2 (будущее):
#   - кросс-компиляция под Pi
#   - on-target smoke tests
# =============================================================================

set working-directory := '..'

[doc('Минимальный CI pipeline: format + host тесты')]
[group('ci')]
pipeline: check-format test
    @echo "  ✅  CI pipeline complete"

[doc('Проверить форматирование')]
[group('ci')]
check-format:
    just build::check-format

[doc('Запустить host unit-тесты')]
[group('ci')]
test:
    just build::test

[doc('Кросс-компиляция под Pi (CI с Pi sysroot в образе)')]
[group('ci')]
build-pi:
    just build::pi
    @echo "  ✅  Pi build artifacts ready"
