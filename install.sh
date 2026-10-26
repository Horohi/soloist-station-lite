#!/bin/sh
set -e

echo "=== Spotify Soloist + Voice/Touch Track Search Installer (aarch64) ==="

# 1. Root check
if [ "$(id -u)" -ne 0 ]; then
    echo "ERROR: Этот скрипт должен выполняться от root на колонке!" >&2
    exit 1
fi

# 2. Arch check
ARCH=$(uname -m)
if [ "$ARCH" != "aarch64" ]; then
    echo "ERROR: Архитектура ядра $ARCH не поддерживается (требуется aarch64)." >&2
    exit 1
fi

# 3. Free space check (need ~40 MB in /data)
FREE_KB=$(df /data | awk 'NR==2 {print $4}')
if [ "$FREE_KB" -lt 35000 ]; then
    echo "ERROR: Недостаточно места в /data (доступно ${FREE_KB} KB, нужно минимум 35000 KB)." >&2
    exit 1
fi

# 4. API Key
API_KEY="${1:-}"
if [ -z "$API_KEY" ]; then
    if [ -f "/data/opt/soloist/start.sh" ]; then
        SAVED_KEY=$(grep -o 'API_KEY="[^"]*"' /data/opt/soloist/start.sh 2>/dev/null | cut -d'"' -f2 || true)
    fi
    if [ -n "$SAVED_KEY" ] && [ "$SAVED_KEY" != "dummy" ]; then
        API_KEY="$SAVED_KEY"
        echo "Найден сохранённый API-ключ: ${API_KEY:0:8}..."
    else
        echo ""
        echo "Для работы требуется Soloist API Key от Spotify."
        echo "Получить его можно за 30 секунд (нужен Spotify Premium):"
        echo "https://developer.spotify.com/dashboard/soloist"
        echo ""
        printf "Введите ваш API-ключ (spak_...): "
        read API_KEY
    fi
fi

if [ -z "$API_KEY" ]; then
    echo "ERROR: API-ключ не может быть пустым!" >&2
    exit 1
fi

# 5. Spotify Web API Credentials (for Alice Voice Track Search)
CLIENT_ID=""
CLIENT_SECRET=""

if [ -f "/data/opt/soloist/spotify_api.conf" ]; then
    SAVED_CID=$(grep -o 'SPOTIFY_CLIENT_ID="[^"]*"' /data/opt/soloist/spotify_api.conf 2>/dev/null | cut -d'"' -f2 || true)
    SAVED_SEC=$(grep -o 'SPOTIFY_CLIENT_SECRET="[^"]*"' /data/opt/soloist/spotify_api.conf 2>/dev/null | cut -d'"' -f2 || true)
fi

if [ -n "$SAVED_CID" ] && [ -n "$SAVED_SEC" ]; then
    CLIENT_ID="$SAVED_CID"
    CLIENT_SECRET="$SAVED_SEC"
    echo "Найдены сохранённые ключи голосового поиска Spotify Web API."
else
    echo ""
    echo "======================================================================="
    echo " 🎙️  НАСТРОЙКА ГОЛОСОВОГО ПОИСКА ТРЕКОВ ЧЕРЕЗ АЛИСУ (SPOTIFY WEB API)"
    echo "======================================================================="
    echo "Чтобы Алиса могла искать ЛЮБЫЕ песни, альбомы и группы по вашему голосу,"
    echo "нужны бесплатные ключи разработчика Spotify Web API (Client ID и Secret)."
    echo ""
    echo "Как получить за 1 минуту:"
    echo "  1. Откройте: https://developer.spotify.com/dashboard"
    echo "  2. Нажмите 'Create app' (любое имя, Redirect URI: http://localhost:8888/callback)"
    echo "  3. Зайдите в Settings и скопируйте Client ID и Client Secret."
    echo ""
    printf "Введите Spotify Client ID (или Enter для пропуска): "
    read CLIENT_ID
    if [ -n "$CLIENT_ID" ]; then
        printf "Введите Spotify Client Secret: "
        read CLIENT_SECRET
    fi

    if [ -z "$CLIENT_ID" ] || [ -z "$CLIENT_SECRET" ]; then
        printf "\n\033[1;31m***********************************************************************\033[0m\n"
        printf "\033[1;31m*                               ВНИМАНИЕ!                             *\033[0m\n"
        printf "\033[1;31m*  ВЫ ПРОПУСТИЛИ ВВОД КЛЮЧЕЙ ГОЛОСОВОГО ПОИСКА SPOTIFY WEB API!       *\033[0m\n"
        printf "\033[1;31m*                                                                     *\033[0m\n"
        printf "\033[1;31m*  Голосовой поиск песен («Алиса, включи песню ...») работать НЕ      *\033[0m\n"
        printf "\033[1;31m*  БУДЕТ! Колонка сможет работать только как обычный Spotify Connect  *\033[0m\n"
        printf "\033[1;31m*  приёмник (выбор треков только через приложение Spotify с телефона).*\033[0m\n"
        printf "\033[1;31m*                                                                     *\033[0m\n"
        printf "\033[1;31m*  Вы сможете добавить ключи позже в:                                 *\033[0m\n"
        printf "\033[1;31m*  /data/opt/soloist/spotify_api.conf                                 *\033[0m\n"
        printf "\033[1;31m***********************************************************************\033[0m\n\n"
        sleep 2
    fi
fi

# 6. Device Name
DEVICE_NAME="${2:-Yandex Station Lite}"

# These values are written into start.sh inside double quotes: refuse characters that would break it
case "$API_KEY" in
    *[!A-Za-z0-9_-]*) echo "ERROR: API-ключ содержит недопустимые символы." >&2; exit 1 ;;
esac
case "$DEVICE_NAME" in
    *[\"\$\`\\]*) echo "ERROR: Имя устройства не должно содержать \" \$ \` или \\." >&2; exit 1 ;;
esac

# 7. Stop previous instances
echo "Остановка старых служб..."
killall -9 soloist soloist-bridge spotiya 2>/dev/null || true
sleep 1

# 7. Create directories
INSTALL_DIR="/data/opt/soloist"
mkdir -p "$INSTALL_DIR/bin" "$INSTALL_DIR/lib64" "$INSTALL_DIR/scripts" "$INSTALL_DIR/data" "/tmp/soloist_cache"

# 8. Copy files
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
echo "Копирование бинарников и библиотек..."
cp -f "$SCRIPT_DIR/bin/soloist" "$INSTALL_DIR/bin/soloist"
if [ -f "$SCRIPT_DIR/bin/soloist-bridge" ]; then
    cp -f "$SCRIPT_DIR/bin/soloist-bridge" "$INSTALL_DIR/bin/soloist-bridge"
    chmod 755 "$INSTALL_DIR/bin/soloist-bridge"
fi
if [ -f "$SCRIPT_DIR/bin/curl" ]; then
    cp -f "$SCRIPT_DIR/bin/curl" "$INSTALL_DIR/bin/curl"
    chmod 755 "$INSTALL_DIR/bin/curl"
fi
cp -af "$SCRIPT_DIR/lib64/"* "$INSTALL_DIR/lib64/"
cp -f "$SCRIPT_DIR/scripts/stop.sh" "$INSTALL_DIR/stop.sh"
cp -f "$SCRIPT_DIR/scripts/status.sh" "$INSTALL_DIR/status.sh"
if [ -f "$SCRIPT_DIR/uninstall.sh" ]; then
    cp -f "$SCRIPT_DIR/uninstall.sh" "$INSTALL_DIR/uninstall.sh"
    chmod 755 "$INSTALL_DIR/uninstall.sh"
fi
if [ -f "$SCRIPT_DIR/scripts/toggle.sh" ]; then
    cp -f "$SCRIPT_DIR/scripts/toggle.sh" "$INSTALL_DIR/scripts/toggle.sh"
    chmod 755 "$INSTALL_DIR/scripts/toggle.sh"
fi
if [ -f "$SCRIPT_DIR/scripts/search_and_play.sh" ]; then
    cp -f "$SCRIPT_DIR/scripts/search_and_play.sh" "$INSTALL_DIR/scripts/search_and_play.sh"
    chmod 755 "$INSTALL_DIR/scripts/search_and_play.sh"
fi

# Spotify Search API Credentials
if [ -n "$CLIENT_ID" ] && [ -n "$CLIENT_SECRET" ]; then
    cat << EOF_CONF > "$INSTALL_DIR/spotify_api.conf"
SPOTIFY_CLIENT_ID="$CLIENT_ID"
SPOTIFY_CLIENT_SECRET="$CLIENT_SECRET"
EOF_CONF
    chmod 600 "$INSTALL_DIR/spotify_api.conf"
    echo "Конфигурация голосового поиска успешно сохранена в $INSTALL_DIR/spotify_api.conf"
elif [ -f "$SCRIPT_DIR/spotify_api.conf" ] && [ ! -f "$INSTALL_DIR/spotify_api.conf" ]; then
    cp -f "$SCRIPT_DIR/spotify_api.conf" "$INSTALL_DIR/spotify_api.conf"
    chmod 600 "$INSTALL_DIR/spotify_api.conf"
elif [ -f "$SCRIPT_DIR/spotify_api.conf.example" ] && [ ! -f "$INSTALL_DIR/spotify_api.conf" ]; then
    cp -f "$SCRIPT_DIR/spotify_api.conf.example" "$INSTALL_DIR/spotify_api.conf.example"
fi

chmod 755 "$INSTALL_DIR/bin/soloist" "$INSTALL_DIR/lib64/ld-linux-aarch64.so.1"
chmod 755 "$INSTALL_DIR/stop.sh" "$INSTALL_DIR/status.sh"

# 9. SSL Certificates & Hosts SNI Proxy (bypass 403 country restrictions)
echo "Настройка SSL-сертификатов и сети..."
mount -o remount,rw / 2>/dev/null || true
# Always put the rootfs back to read-only, even if a later step fails (set -e)
trap 'mount -o remount,ro / 2>/dev/null || true' EXIT

if [ ! -f "/etc/ssl/certs/ca-certificates.crt" ]; then
    mkdir -p /etc/ssl/certs
    if [ -f "/system/vendor/quasar/ca-certificates.crt" ]; then
        ln -sf /system/vendor/quasar/ca-certificates.crt /etc/ssl/certs/ca-certificates.crt
    fi
fi

if ! grep -q "5.175.161.40" /etc/hosts 2>/dev/null; then
    cat << 'HOSTS_EOF' >> /etc/hosts

# Spotify SNI Proxy (Bypass 403 geoblock)
5.175.161.40 accounts.spotify.com
5.175.161.40 api.spotify.com
5.175.161.40 open.spotify.com
5.175.161.40 spclient.wg.spotify.com
HOSTS_EOF
fi

# 10. Generate start.sh
echo "Генерация start.sh..."
cat << START_EOF > "$INSTALL_DIR/start.sh"
#!/bin/sh
set -e
mkdir -p /data/opt/soloist/data /tmp/soloist_cache /data/opt/soloist/scripts
/data/opt/soloist/stop.sh >/dev/null 2>&1 || true

for i in \$(seq 1 45); do
    if ip -4 addr show wlan0 2>/dev/null | grep -q "inet "; then
        if ping -c 1 -W 1 1.1.1.1 >/dev/null 2>&1 || ping -c 1 -W 1 8.8.8.8 >/dev/null 2>&1; then
            break
        fi
    fi
    sleep 1
done
sleep 2

export LD_LIBRARY_PATH=/data/opt/soloist/lib64:\$LD_LIBRARY_PATH
export APULSE_PLAYBACK_DEVICE=plug:default
export APULSE_DIAG=0
export CURL_CA_BUNDLE=/etc/ssl/certs/ca-certificates.crt
export SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt

API_KEY="$API_KEY"

nohup /data/opt/soloist/bin/soloist \\
    -n "$DEVICE_NAME" \\
    -k "\$API_KEY" \\
    -D /data/opt/soloist/data \\
    -C /tmp/soloist_cache \\
    -w 127.0.0.1:9090 \\
    --initial-volume 100 \\
    -v > /tmp/soloist.log 2>&1 &

echo \$! > /data/opt/soloist/data/soloist.pid
echo "Spotify Soloist (arm64) started, PID=\$(cat /data/opt/soloist/data/soloist.pid)"

sleep 1

if [ -x /data/opt/soloist/bin/soloist-bridge ]; then
    /usr/bin/setsid /data/opt/soloist/bin/soloist-bridge > /tmp/soloist-bridge.log 2>&1 < /dev/null &
    echo \$! > /data/opt/soloist/data/bridge.pid
    echo "Soloist Bridge (Fast Control + Full Search) started, PID=\$(cat /data/opt/soloist/data/bridge.pid)"
fi

# Activation & RAM cache guard
(
    # 1. Wait for daemon to become ready and activate device if logged in
    for attempt in \$(seq 1 15); do
        sleep 2
        STATUS=\$(/data/opt/soloist/bin/soloist ctl -w 127.0.0.1:9090 status 2>/dev/null || true)
        if echo "\$STATUS" | grep -qi "logged in: yes"; then
            /data/opt/soloist/bin/soloist ctl -w 127.0.0.1:9090 activate >/dev/null 2>&1 || true
            break
        fi
    done

    # 2. RAM guard: keep audio stream cache under 20MB so tmpfs never exhausts RAM
    while true; do
        sleep 120
        if [ -d /tmp/soloist_cache/cache ]; then
            CACHE_KB=\$(du -sk /tmp/soloist_cache/cache 2>/dev/null | awk '{print \$1}')
            if [ "\${CACHE_KB:-0}" -gt 20480 ]; then
                find /tmp/soloist_cache/cache -type f -amin +10 -delete 2>/dev/null || true
                CACHE_KB_AFTER=\$(du -sk /tmp/soloist_cache/cache 2>/dev/null | awk '{print \$1}')
                if [ "\${CACHE_KB_AFTER:-0}" -gt 20480 ]; then
                    rm -rf /tmp/soloist_cache/cache/* 2>/dev/null || true
                fi
            fi
        fi
    done
) &
START_EOF
chmod 700 "$INSTALL_DIR/start.sh"   # contains the Soloist API key

# 11. Autostart setup in init.d
echo "Настройка автозапуска при включении колонки..."
if [ -f "$SCRIPT_DIR/scripts/S99spotify-connect" ]; then
    cp -f "$SCRIPT_DIR/scripts/S99spotify-connect" /etc/init.d/S99spotify-connect
    chmod 755 /etc/init.d/S99spotify-connect
fi

# Make rootfs read-only again
mount -o remount,ro / 2>/dev/null || true

# 12. Start service
echo "Запуск службы..."
"$INSTALL_DIR/start.sh"
sleep 2

echo ""
echo "=== Установка успешно завершена! ==="
echo "Имя устройства в Spotify: $DEVICE_NAME"
echo ""
echo "Управление голосом:"
echo "  • «Алиса, включи [название трека или исполнителя]»  -> Поиск и воспроизведение в Spotify"
echo "  • «Алиса, поставь [песню / артиста]»                -> Поиск и воспроизведение в Spotify"
echo "  • «Алиса, найди [песню / группу]»                  -> Поиск и воспроизведение в Spotify"
echo "  • «Алиса, пауза» / «стоп» / «хватит»               -> Пауза"
echo "  • «Алиса, дальше» / «следующий трек»              -> Следующий трек"
echo "  • «Алиса, назад» / «предыдущий трек»              -> Предыдущий трек"
echo "  • «Алиса, перемешай» / «в случайном порядке»       -> Режим случайного воспроизведения"
echo "  • «Алиса, по порядку»                             -> Выключить перемешивание"
echo "  • «Алиса, зацикли» / «повторяй»                    -> Повтор трека"
echo "  • «Алиса, громче» / «тише»                         -> Громкость"
echo ""
echo "Сенсорная кнопка на колонке:"
echo "  • Короткое касание центра: Play / Pause"
echo ""
echo "Автозагрузка:"
echo "  • Служба запускается автоматически при включении колонки через /etc/init.d/S99spotify-connect"
echo ""
echo "Управление службой в консоли:"
echo "  Статус:  /data/opt/soloist/status.sh"
echo "  Стоп:    /data/opt/soloist/stop.sh"
echo "  Старт:   /data/opt/soloist/start.sh"
echo "  Логи:    tail -f /tmp/soloist.log /tmp/soloist-search.log"
echo "=========================================================="
