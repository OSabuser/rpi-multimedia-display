\# Фаза PRE6 (OLD) — systemd + lifecycle (1–2 дня) 

**Цель:** корректный запуск с нуля, supervision, логи в journald. 

- [ ] Все `.service` файлы в `deploy/` (включая `indicator-setup.service`) 
- [ ]  `indicator.target` → autostart через `multi-user.target`
- [ ]  Проверка: power on → система работает через N секунд (замерить) 
- [ ]  Проверка: `systemctl kill indicator` → перезапуск за 2 с ---