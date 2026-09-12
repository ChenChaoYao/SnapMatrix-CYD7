#pragma once

#include <Arduino.h>
#include "microlink.h"
#include "microlink_internal.h"

typedef std::function<void(bool connected, const String &vpnIp)> TailscaleStateCallback;

class TailscaleService {
public:
    static TailscaleService& instance() {
        static TailscaleService _instance;
        return _instance;
    }

    void begin(bool enabled, const String &authKey, const String &deviceName = "snapmatrix-cyd", 
               const String &targetPeerIp = "", const String &subnetRouterIp = "") {
        _enabled = enabled;
        _authKey = authKey;
        _deviceName = deviceName;
        _rawPrinterIpStr = targetPeerIp;
        _subnetRouterIpStr = subnetRouterIp;
        _authKey.trim();
        _deviceName.trim();
        _rawPrinterIpStr.trim();
        _subnetRouterIpStr.trim();

        // 判斷是否使用 Subnet Router 代理模式
        if (_subnetRouterIpStr.length() > 0 && !_rawPrinterIpStr.startsWith("100.")) {
            _isSubnetMode = true;
            _targetPeerIpStr = _subnetRouterIpStr; // 喚醒與連線對象為 Subnet Router
            _targetLanIp = microlink_parse_ip(_rawPrinterIpStr.c_str());
        } else {
            _isSubnetMode = false;
            _targetPeerIpStr = _rawPrinterIpStr;
            _targetLanIp = 0;
        }

        _targetPeerIp = microlink_parse_ip(_targetPeerIpStr.c_str());

        if (!_enabled) {
            Serial.println("[Tailscale] 服務未啟用 (使用區網直連模式)");
            return;
        }

        if (_authKey.length() == 0) {
            Serial.println("[Tailscale] 錯誤: 已啟用 Tailscale 但未提供 Auth Key (請於 WebUI 設定)");
            return;
        }

        if (_ml != nullptr) {
            Serial.println("[Tailscale] 正在重啟 Tailscale 服務...");
            stop();
        }

        Serial.printf("[Tailscale] 正在初始化 Tailscale 客戶端 (裝置: %s, 目標 Peer: %s, Subnet模式: %s)...\n", 
                      _deviceName.c_str(), _targetPeerIpStr.c_str(), _isSubnetMode ? "啟用" : "關閉");

        microlink_config_t config;
        memset(&config, 0, sizeof(config));
        config.auth_key = _authKey.c_str();
        config.device_name = _deviceName.c_str();
        config.enable_derp = true;
        config.enable_stun = true;
        config.enable_disco = true;
        config.max_peers = 16;
        config.wifi_tx_power_dbm = 0;
        config.priority_peer_ip = _targetPeerIp;

        _ml = microlink_init(&config);
        if (!_ml) {
            Serial.println("[Tailscale] 初始化失敗 (microlink_init returned NULL)");
            return;
        }

        // 註冊狀態回呼
        microlink_set_state_callback(_ml, onStateChangedStatic, this);

        esp_err_t err = microlink_start(_ml);
        if (err != ESP_OK) {
            Serial.printf("[Tailscale] 連線啟動失敗: err=0x%x\n", err);
            return;
        }

        Serial.println("[Tailscale] 連線任務已在背景啟動，正在與 Tailscale Control Plane 協同連線...");
    }

    void stop() {
        if (_ml) {
            Serial.println("[Tailscale] 停止 Tailscale 連線...");
            microlink_stop(_ml);
            microlink_destroy(_ml);
            _ml = nullptr;
            _connected = false;
            _vpnIp = "";
            _peerTunnelUp = false;
        }
    }

    void setTargetPeer(const String &peerIp, const String &subnetRouterIp = "") {
        _rawPrinterIpStr = peerIp;
        _subnetRouterIpStr = subnetRouterIp;
        _rawPrinterIpStr.trim();
        _subnetRouterIpStr.trim();

        if (_subnetRouterIpStr.length() > 0 && !_rawPrinterIpStr.startsWith("100.")) {
            _isSubnetMode = true;
            _targetPeerIpStr = _subnetRouterIpStr;
            _targetLanIp = microlink_parse_ip(_rawPrinterIpStr.c_str());
        } else {
            _isSubnetMode = false;
            _targetPeerIpStr = _rawPrinterIpStr;
            _targetLanIp = 0;
        }

        _targetPeerIp = microlink_parse_ip(_targetPeerIpStr.c_str());
        _peerTunnelUp = false;
        if (_connected && _targetPeerIp != 0) {
            ensurePeerHandshake(_targetPeerIpStr);
        }
    }

    void ensurePeerHandshake(const String &peerIp = "") {
        if (!_enabled || !_connected || !_ml) return;
        uint32_t dest = 0;
        if (peerIp.length() > 0 && !_isSubnetMode) {
            dest = microlink_parse_ip(peerIp.c_str());
        } else {
            dest = _targetPeerIp;
        }
        if (dest != 0) {
            ml_wg_mgr_trigger_handshake(_ml, dest);
            ml_wg_mgr_send_cmm(_ml, dest);
        }
    }

    bool isPeerUp(const String &peerIp = "") {
        if (!_enabled || !_connected || !_ml) return false;
        uint32_t dest = 0;
        if (peerIp.length() > 0 && !_isSubnetMode) {
            dest = microlink_parse_ip(peerIp.c_str());
        } else {
            dest = _targetPeerIp;
        }
        if (dest == 0) return false;
        return ml_wg_mgr_peer_is_up(_ml, dest);
    }

    void update() {
        if (!_enabled || !_ml) return;

        static unsigned long last_check = 0;
        if (millis() - last_check > 2000) {
            last_check = millis();
            checkState();
        }

        // 當已連接 Tailscale 且設定了目標 Peer (Subnet Router 或直連 Peer) 時，確保 WireGuard 隧道打通
        if (_connected && _targetPeerIp != 0) {
            static unsigned long last_hs_trigger = 0;
            bool peer_up = ml_wg_mgr_peer_is_up(_ml, _targetPeerIp);
            if (!peer_up) {
                if (millis() - last_hs_trigger > 3000) {
                    last_hs_trigger = millis();
                    Serial.printf("[Tailscale] 正在向目標節點 (%s) 發送 WireGuard Handshake 與 DISCO CMM...\n", 
                                  _targetPeerIpStr.c_str());
                    ml_wg_mgr_trigger_handshake(_ml, _targetPeerIp);
                    ml_wg_mgr_send_cmm(_ml, _targetPeerIp);
                }
            } else if (!_peerTunnelUp) {
                _peerTunnelUp = true;
                Serial.printf("[Tailscale] ★ 與目標節點 (%s) 的 WireGuard 加密隧道已成功打通！\n", 
                              _targetPeerIpStr.c_str());

                // 若為 Subnet 代理模式，將目標 LAN IP 綁定至該 Router Peer 的 AllowedIPs 與 lwIP
                if (_isSubnetMode && _targetLanIp != 0) {
                    Serial.printf("[Tailscale Subnet] 正在為目標印表機 (%s) 掛載子網路由至網關 (%s)...\n",
                                  _rawPrinterIpStr.c_str(), _subnetRouterIpStr.c_str());
                    ml_wg_mgr_add_subnet_route(_ml, _targetPeerIp, _targetLanIp, 0);
                }

                if (_userCallback) {
                    _userCallback(true, _vpnIp);
                }
            }
        }
    }

    bool isEnabled() const { return _enabled; }
    bool isConnected() const { return _connected; }
    bool isTargetPeerUp() const { return _peerTunnelUp; }
    String getVpnIp() const { return _vpnIp; }
    microlink_state_t getState() const {
        if (!_ml) return ML_STATE_IDLE;
        return microlink_get_state(_ml);
    }

    String getDebugJson() const {
        if (!_ml) return "{}";
        String out = "{";
        out += "\"peer_count\":" + String(_ml->peer_count);
        out += ",\"derp_connected\":" + String(_ml->derp.connected ? "true" : "false");
        out += ",\"derp_home\":" + String(_ml->derp_home_region);
        out += ",\"target_ip\":\"" + _targetPeerIpStr + "\"";
        out += ",\"subnet_mode\":" + String(_isSubnetMode ? "true" : "false");
        out += ",\"raw_printer_ip\":\"" + _rawPrinterIpStr + "\"";
        out += ",\"subnet_router_ip\":\"" + _subnetRouterIpStr + "\"";
        
        int target_idx = -1;
        for (int i = 0; i < _ml->peer_count; i++) {
            if (_ml->peers[i].active && _ml->peers[i].vpn_ip == _targetPeerIp) {
                target_idx = i;
                break;
            }
        }
        out += ",\"target_found\":" + String(target_idx >= 0 ? "true" : "false");
        if (target_idx >= 0) {
            const ml_peer_t *p = &_ml->peers[target_idx];
            out += ",\"target_name\":\"" + String(p->hostname) + "\"";
            out += ",\"target_wg_idx\":" + String(p->wg_peer_index);
            out += ",\"target_direct\":" + String(p->has_direct_path ? "true" : "false");
            out += ",\"target_derp_region\":" + String(p->derp_region);
            out += ",\"target_eps\":" + String(p->endpoint_count);
            out += ",\"target_is_up\":" + String(ml_wg_mgr_peer_is_up(_ml, _targetPeerIp) ? "true" : "false");
        }
        out += "}";
        return out;
    }

    String getStateString() const {
        if (!_enabled) return "未啟用";
        if (!_ml) return "未初始化";
        switch (microlink_get_state(_ml)) {
            case ML_STATE_IDLE: return "閒置 (IDLE)";
            case ML_STATE_WIFI_WAIT: return "等待 WiFi";
            case ML_STATE_CONNECTING: return "連線中...";
            case ML_STATE_REGISTERING: return "註冊 Tailnet 中...";
            case ML_STATE_CONNECTED: 
                if (_targetPeerIp != 0 && !_peerTunnelUp) {
                    return "已連線 (" + _vpnIp + ") - 隧道建立中";
                }
                if (_isSubnetMode) {
                    return "已連線 (" + _vpnIp + ") - 子網代理";
                }
                return "已連線 (" + _vpnIp + ")";
            case ML_STATE_RECONNECTING: return "重新連線中...";
            case ML_STATE_ERROR: return "連線錯誤";
            default: return "未知狀態";
        }
    }

    void onStateChange(TailscaleStateCallback cb) {
        _userCallback = cb;
    }

private:
    TailscaleService() : _enabled(false), _connected(false), _ml(nullptr), 
                         _vpnIp(""), _targetPeerIp(0), _peerTunnelUp(false),
                         _isSubnetMode(false), _targetLanIp(0) {}
    ~TailscaleService() { stop(); }

    bool _enabled;
    String _authKey;
    String _deviceName;
    String _targetPeerIpStr;
    String _rawPrinterIpStr;
    String _subnetRouterIpStr;
    uint32_t _targetPeerIp;
    uint32_t _targetLanIp;
    bool _isSubnetMode;
    bool _connected;
    bool _peerTunnelUp;
    String _vpnIp;
    microlink_t *_ml;
    TailscaleStateCallback _userCallback;

    static void onStateChangedStatic(microlink_t *ml, microlink_state_t state, void *user_data) {
        TailscaleService *self = static_cast<TailscaleService*>(user_data);
        if (self) {
            self->handleStateChanged(state);
        }
    }

    void handleStateChanged(microlink_state_t state) {
        if (state == ML_STATE_CONNECTED) {
            _connected = true;
            uint32_t ip = microlink_get_vpn_ip(_ml);
            char ip_buf[16] = {0};
            microlink_ip_to_str(ip, ip_buf);
            _vpnIp = String(ip_buf);
            Serial.printf("[Tailscale] ★ 成功連線至 Tailnet！獲派 VPN IP: %s\n", _vpnIp.c_str());
            
            if (_targetPeerIp != 0) {
                Serial.printf("[Tailscale] 立即向印表機 (%s) 發送喚醒握手...\n", _targetPeerIpStr.c_str());
                ml_wg_mgr_trigger_handshake(_ml, _targetPeerIp);
                ml_wg_mgr_send_cmm(_ml, _targetPeerIp);
            }

            if (_userCallback) {
                _userCallback(true, _vpnIp);
            }
        } else if (_connected && state != ML_STATE_CONNECTED) {
            _connected = false;
            _peerTunnelUp = false;
            Serial.printf("[Tailscale] 失去 Tailscale 連線，狀態碼: %d\n", (int)state);
            if (_userCallback) {
                _userCallback(false, "");
            }
        }
    }

    void checkState() {
        if (!_ml) return;
        bool conn = microlink_is_connected(_ml);
        if (conn && !_connected) {
            handleStateChanged(ML_STATE_CONNECTED);
        } else if (!conn && _connected) {
            handleStateChanged(microlink_get_state(_ml));
        }
    }
};

extern TailscaleService &tailscaleService;
