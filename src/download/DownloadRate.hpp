#pragma once
#include <QtGlobal>
#include <algorithm>
#include <cmath>

// Display-only telemetry. Never use this smoothing for retry or stall decisions.
// Inputs are monotonic time and actual received bytes, not restored cache progress.
class DownloadRate {
public:
    void reset(qint64 now = 0, qint64 received = 0) {
        m_start = m_last = m_lastData = now;
        m_received = received;
        m_speed = m_etaSpeed = 0;
        m_eta = -1;
        m_etaAt = now;
        m_ready = false;
    }
    void sample(qint64 now, qint64 received) {
        if (now < m_last || received < m_received) { reset(now, received); return; }
        const qint64 elapsed = now - m_last;
        if (elapsed < 1000) return;
        const qint64 delta = received - m_received;
        const double rate = double(delta) * 1000.0 / double(elapsed);
        if (delta > 0) m_lastData = now;
        if (!m_ready && delta > 0) {
            m_speed = m_etaSpeed = rate;
            m_ready = true;
        } else if (m_ready) {
            m_speed += (1.0 - std::exp(-double(elapsed) / 3000.0)) * (rate - m_speed);
            m_etaSpeed += (1.0 - std::exp(-double(elapsed) / 10000.0)) * (rate - m_etaSpeed);
        }
        m_last = now;
        m_received = received;
    }
    qint64 bytesPerSecond(qint64 now) const {
        if (!m_ready) return -1;
        return now - m_lastData >= 6000 ? 0 : qint64(m_speed);
    }
    qint64 remainingSeconds(qint64 now, qint64 remainingBytes) {
        if (remainingBytes < 0 || !m_ready || now - m_start < 2000
            || bytesPerSecond(now) <= 0 || m_etaSpeed <= 0) {
            m_eta = -1;
            return -1;
        }
        if (remainingBytes == 0) return 0;
        const double raw = double(remainingBytes) / m_etaSpeed;
        if (!std::isfinite(raw) || raw >= double(Q_INT64_C(0x7fffffffffffffff))) return -1;
        if (m_eta < 0) m_eta = raw;
        else {
            const double elapsed = double(std::max<qint64>(0, now - m_etaAt)) / 1000.0;
            const double countdown = std::max(0.0, m_eta - elapsed);
            m_eta = countdown + (1.0 - std::exp(-elapsed / 6000.0)) * (raw - countdown);
        }
        m_etaAt = now;
        return std::max<qint64>(1, qint64(std::ceil(m_eta)));
    }
private:
    qint64 m_start = 0, m_last = 0, m_lastData = 0, m_received = 0, m_etaAt = 0;
    double m_speed = 0, m_etaSpeed = 0, m_eta = -1;
    bool m_ready = false;
};
