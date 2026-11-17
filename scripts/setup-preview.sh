#!/bin/sh
# A safe, illustrative setup walkthrough: no network, credentials or writes.
printf '\n=== Soloist: просмотр установки (демо) ===\n'
printf 'Это только пример: колонка и настройки не изменяются.\n\n'
printf 'Обнаружена колонка: Yandex Station Lite (пример).\n\n'
printf '1. Soloist API Key (Spotify Connect)\n'
printf '   В демо ключ не нужен. В реальной установке потребуется личный ключ.\n'
printf '   Ключ [тестовый пример]: '; IFS= read -r answer || true
printf '\n2. Имя колонки в приложении Spotify\n'
printf '   Имя устройства [Yandex Station Lite]: '; IFS= read -r answer || true
printf '\n3. Ключи Spotify Web API (для голосового поиска Алисы, опционально)\n'
printf '   Client ID [Enter — пропустить]: '; IFS= read -r answer || true
printf '\n4. Плейлист «Любимые треки» (опционально)\n'
printf '   URI/ссылка [Enter — пропустить]: '; IFS= read -r answer || true
printf '\n5. Автоматический Spotify-прокси\n'
printf '   Spotify API недоступен; рекомендуется автовыбор (пример).\n'
printf '   Включить автоматический Spotify-прокси? [Y/n]: '; IFS= read -r answer || true
printf '\n=== Пример результата ===\n'
printf 'Колонка: Yandex Station Lite\n'
printf 'Сеть: auto; проверка раз в час; рабочий адрес сохраняется\n'
printf 'При отсутствии доступных узлов текущие записи сохраняются\n'
printf 'Spotify Web API: пропущен; поиск по названию не работает\n'
printf '\nПример spotify-network.conf:\n  SOLOIST_NETWORK_MODE="auto"\n'
printf '\nДемо завершено. Никакие файлы не изменены.\n'
