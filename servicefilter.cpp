#include "servicefilter.hpp"
#include <algorithm>

CServiceFilter::CServiceFilter()
    : m_programNumberOrIndex(0)
    , m_audio1Mode(0)
    , m_audio2Mode(0)
    , m_captionMode(0)
    , m_superimposeMode(0)
    , m_videoPid(0)
    , m_audio1Pid(0)
    , m_audio2Pid(0)
    , m_captionPid(0)
    , m_superimposePid(0)
    , m_pcrPid(0)
    , m_pcr(-1)
    , m_patCounter(0)
    , m_pmtCounter(0)
    , m_audio1PesCounter(0)
    , m_audio2PesCounter(0)
    , m_audio1PesCounterBase(-2)
    , m_audio2PesCounterBase(-2)
    , m_audio1Pts(-1)
    , m_audio2Pts(-1)
    , m_audio1PtsPcrDiff(0)
    , m_audio2PtsPcrDiff(-1)
{
    static const PAT zeroPat = {};
    m_pat = zeroPat;
    m_pmtPsi = zeroPat.psi;
}

void CServiceFilter::AddPacket(const uint8_t *packet)
{
    if (m_programNumberOrIndex == 0) {
        m_packets.insert(m_packets.end(), packet, packet + 188);
        return;
    }

    int unitStart = extract_ts_header_unit_start(packet);
    int pid = extract_ts_header_pid(packet);
    int adaptation = extract_ts_header_adaptation(packet);
    int counter = extract_ts_header_counter(packet);
    int payloadSize = get_ts_payload_size(packet);
    const uint8_t *payload = packet + 188 - payloadSize;

    if (pid == 0) {
        extract_pat(&m_pat, payload, payloadSize, unitStart, counter);
        auto itPmt = FindTargetPmtRef(m_pat.pmt);
        if (itPmt != m_pat.pmt.end()) {
            if (unitStart) {
                AddPat(m_pat.transport_stream_id, itPmt->program_number, FindNitRef(m_pat.pmt) != m_pat.pmt.end());
            }
        }
        else {
            m_videoPid = 0;
            m_audio1Pid = 0;
            m_audio2Pid = 0;
            m_captionPid = 0;
            m_superimposePid = 0;
            m_pcrPid = 0;
            m_pcr = -1;
        }
    }
    else {
        auto itPmt = FindTargetPmtRef(m_pat.pmt);
        if (itPmt != m_pat.pmt.end()) {
            if (pid == itPmt->pmt_pid) {
                int done;
                do {
                    done = extract_psi(&m_pmtPsi, payload, payloadSize, unitStart, counter);
                    if (m_pmtPsi.version_number && m_pmtPsi.table_id == 2 && m_pmtPsi.current_next_indicator) {
                        AddPmt(m_pmtPsi);
                    }
                }
                while (!done);
            }
            if (pid == m_pcrPid) {
                if (adaptation & 2) {
                    int adaptationLength = packet[4];
                    if (adaptationLength >= 6 && !!(packet[5] & 0x10)) {
                        if (pid != m_videoPid &&
                            pid != m_audio1Pid &&
                            pid != m_audio2Pid &&
                            pid != m_captionPid &&
                            pid != m_superimposePid) {
                            AddPcrAdaptation(packet + 6);
                        }
                        m_pcr = (packet[10] >> 7) |
                                (packet[9] << 1) |
                                (packet[8] << 9) |
                                (packet[7] << 17) |
                                (static_cast<int64_t>(packet[6]) << 25);
                        if (m_audio1Mode == 1 && m_audio1Pid == 0) {
                            m_audio1PesCounterBase = -1;
                            AddAudioPesPackets(0, (m_pcr + m_audio1PtsPcrDiff) & 0x1ffffffff, m_audio1Pts, m_audio1PesCounter);
                        }
                        if (m_audio2Mode == 1 && m_audio2Pid == 0) {
                            if (m_audio2PtsPcrDiff < 0) {
                                m_audio2PtsPcrDiff = m_audio1PtsPcrDiff;
                            }
                            m_audio2PesCounterBase = -1;
                            AddAudioPesPackets(1, (m_pcr + m_audio2PtsPcrDiff) & 0x1ffffffff, m_audio2Pts, m_audio2PesCounter);
                        }
                    }
                }
            }
            if (pid == m_videoPid) {
                ChangePidAndAddPacket(packet, 0x0100);
            }
            else if (pid == m_audio1Pid) {
                int64_t pts = GetAudioPresentationTimeStamp(unitStart, payload, payloadSize);
                if (pts >= 0 && m_pcr >= 0) {
                    m_audio1PtsPcrDiff = 0x200000000 + pts - m_pcr;
                }
                if (m_audio1PesCounterBase < 0) {
                    // Keep counter continuity
                    m_audio1PesCounterBase = m_audio1PesCounterBase < -1 ? 0 : (0x10 + m_audio1PesCounter + 1 - counter) & 0x0f;
                }
                m_audio1PesCounter = (m_audio1PesCounterBase + counter) & 0x0f;
                ChangePidAndAddPacket(packet, 0x0110, m_audio1PesCounter);
            }
            else if (pid == m_audio2Pid) {
                int64_t pts = GetAudioPresentationTimeStamp(unitStart, payload, payloadSize);
                if (pts >= 0 && m_pcr >= 0) {
                    m_audio2PtsPcrDiff = 0x200000000 + pts - m_pcr;
                }
                if (m_audio2PesCounterBase < 0) {
                    // Keep counter continuity
                    m_audio2PesCounterBase = m_audio2PesCounterBase < -1 ? 0 : (0x10 + m_audio2PesCounter + 1 - counter) & 0x0f;
                }
                m_audio2PesCounter = (m_audio2PesCounterBase + counter) & 0x0f;
                ChangePidAndAddPacket(packet, 0x0111, m_audio2PesCounter);
            }
            else if (pid == m_captionPid) {
                ChangePidAndAddPacket(packet, 0x0130);
            }
            else if (pid == m_superimposePid) {
                ChangePidAndAddPacket(packet, 0x0138);
            }
            else if (pid < 0x0030) {
                m_packets.insert(m_packets.end(), packet, packet + 188);
            }
            else {
                auto itNit = FindNitRef(m_pat.pmt);
                if (itNit != m_pat.pmt.end() && pid == itNit->pmt_pid) {
                    // NIT pid should be 0x0010. This case is unusual.
                    ChangePidAndAddPacket(packet, 0x0010);
                }
            }
        }
    }
}

std::vector<PMT_REF>::const_iterator CServiceFilter::FindNitRef(const std::vector<PMT_REF> &pmt)
{
    return std::find_if(pmt.begin(), pmt.end(), [](const PMT_REF &a) { return a.program_number == 0; });
}

std::vector<PMT_REF>::const_iterator CServiceFilter::FindTargetPmtRef(const std::vector<PMT_REF> &pmt) const
{
    if (m_programNumberOrIndex < 0) {
        int index = -m_programNumberOrIndex;
        for (auto it = pmt.begin(); it != pmt.end(); ++it) {
            if (it->program_number != 0) {
                if (--index == 0) {
                    return it;
                }
            }
        }
        return pmt.end();
    }
    return std::find_if(pmt.begin(), pmt.end(), [=](const PMT_REF &a) { return a.program_number == m_programNumberOrIndex; });
}

void CServiceFilter::AddPat(int transportStreamID, int programNumber, bool addNit)
{
    // Create PAT
    m_buf.assign(9, 0);
    m_buf[1] = 0x00;
    m_buf[2] = 0xb0;
    m_buf[3] = addNit ? 17 : 13;
    m_buf[4] = static_cast<uint8_t>(transportStreamID >> 8);
    m_buf[5] = static_cast<uint8_t>(transportStreamID);
    m_buf[6] = m_lastPat.size() > 6 ? m_lastPat[6] : 0xc1;
    if (addNit) {
        m_buf.push_back(0);
        m_buf.push_back(0);
        m_buf.push_back(0xe0);
        m_buf.push_back(0x10);
    }
    m_buf.push_back(static_cast<uint8_t>(programNumber >> 8));
    m_buf.push_back(static_cast<uint8_t>(programNumber));
    // PMT_PID=0x01f0
    m_buf.push_back(0xe1);
    m_buf.push_back(0xf0);
    if (m_lastPat.size() == m_buf.size() + 4 &&
        std::equal(m_buf.begin(), m_buf.end(), m_lastPat.begin())) {
        // Copy CRC
        m_buf.insert(m_buf.end(), m_lastPat.end() - 4, m_lastPat.end());
    }
    else {
        // Increment version number
        m_buf[6] = 0xc1 | (((m_buf[6] >> 1) + 1) & 0x1f) << 1;
        uint32_t crc = calc_crc32(m_buf.data() + 1, static_cast<int>(m_buf.size() - 1));
        m_buf.push_back(crc >> 24);
        m_buf.push_back((crc >> 16) & 0xff);
        m_buf.push_back((crc >> 8) & 0xff);
        m_buf.push_back(crc & 0xff);
        m_lastPat = m_buf;
    }

    // Create TS packet
    m_packets.push_back(0x47);
    m_packets.push_back(0x40);
    m_packets.push_back(0x00);
    m_patCounter = (m_patCounter + 1) & 0x0f;
    m_packets.push_back(0x10 | m_patCounter);
    m_packets.insert(m_packets.end(), m_buf.begin(), m_buf.end());
    m_packets.resize((m_packets.size() / 188 + 1) * 188, 0xff);
}

void CServiceFilter::AddPmt(const PSI &psi)
{
    const uint8_t H_262_VIDEO = 0x02;
    const uint8_t PES_PRIVATE_DATA = 0x06;
    const uint8_t ADTS_TRANSPORT = 0x0f;
    const uint8_t AVC_VIDEO = 0x1b;
    const uint8_t H_265_VIDEO = 0x24;

    if (psi.section_length < 9) {
        return;
    }
    const uint8_t *table = psi.data;
    int programNumber = (table[3] << 8) | table[4];
    m_pcrPid = ((table[8] & 0x03) << 8) | table[9];
    if (m_pcrPid == 0x1fff) {
        m_pcr = -1;
    }
    int programInfoLength = ((table[10] & 0x03) << 8) | table[11];
    int pos = 3 + 9 + programInfoLength;
    if (psi.section_length < pos) {
        return;
    }

    // Create PMT
    m_buf.assign(13, 0);
    m_buf[1] = 0x02;
    m_buf[4] = static_cast<uint8_t>(programNumber >> 8);
    m_buf[5] = static_cast<uint8_t>(programNumber);
    m_buf[6] = m_lastPmt.size() > 6 ? m_lastPmt[6] : 0xc1;
    // PCR_PID=0x01ff
    m_buf[9] = 0xe1;
    m_buf[10] = 0xff;
    m_buf[11] = 0xc0 | static_cast<uint8_t>(programInfoLength >> 8);
    m_buf[12] = static_cast<uint8_t>(programInfoLength);
    // Copy 1st descriptor loop
    m_buf.insert(m_buf.end(), table + 12, table + pos);

    int lastAudio1Pid = m_audio1Pid;
    int lastAudio2Pid = m_audio2Pid;
    m_videoPid = 0;
    m_audio1Pid = 0;
    m_audio2Pid = 0;
    m_captionPid = 0;
    m_superimposePid = 0;
    int m_videoDescPos = 0;
    int m_audio1DescPos = 0;
    int m_audio2DescPos = 0;
    int m_captionDescPos = 0;
    int m_superimposeDescPos = 0;
    bool maybeCProfile = false;
    bool m_audio1ComponentTagUnknown = true;

    int tableLen = 3 + psi.section_length - 4/*CRC32*/;
    while (pos + 4 < tableLen) {
        int streamType = table[pos];
        int esPid = ((table[pos + 1] & 0x1f) << 8) | table[pos + 2];
        int esInfoLength = ((table[pos + 3] & 0x03) << 8) | table[pos + 4];
        if (pos + 5 + esInfoLength <= tableLen) {
            int componentTag = 0xff;
            for (int i = pos + 5; i + 2 < pos + 5 + esInfoLength; i += 2 + table[i + 1]) {
                // stream_identifier_descriptor
                if (table[i] == 0x52) {
                    componentTag = table[i + 2];
                    break;
                }
            }
            if (streamType == H_262_VIDEO ||
                streamType == AVC_VIDEO ||
                streamType == H_265_VIDEO) {
                if ((m_videoPid == 0 && componentTag == 0xff) || componentTag == 0x00 || componentTag == 0x81) {
                    m_videoPid = esPid;
                    m_videoDescPos = pos;
                    maybeCProfile = componentTag == 0x81;
                }
            }
            else if (streamType == ADTS_TRANSPORT) {
                if ((m_audio1Pid == 0 && componentTag == 0xff) || componentTag == 0x10 || componentTag == 0x83 || componentTag == 0x85) {
                    m_audio1Pid = esPid;
                    m_audio1DescPos = pos;
                    m_audio1ComponentTagUnknown = componentTag == 0xff;
                }
                else if (componentTag == 0x11) {
                    if (m_audio2Mode != 2) {
                        m_audio2Pid = esPid;
                        m_audio2DescPos = pos;
                    }
                }
            }
            else if (streamType == PES_PRIVATE_DATA) {
                if (componentTag == 0x30 || componentTag == 0x87) {
                    if (m_captionMode != 2) {
                        m_captionPid = esPid;
                        m_captionDescPos = pos;
                    }
                }
                else if (componentTag == 0x38 || componentTag == 0x88) {
                    if (m_superimposeMode != 2) {
                        m_superimposePid = esPid;
                        m_superimposeDescPos = pos;
                    }
                }
            }
        }
        pos += 5 + esInfoLength;
    }

    if (m_audio1Pid != lastAudio1Pid) {
        m_audio1Pts = -1;
    }
    if (m_audio2Pid != lastAudio2Pid) {
        m_audio2Pts = -1;
    }

    if (m_videoPid != 0) {
        m_buf.push_back(table[m_videoDescPos]);
        // PID=0x0100
        m_buf.push_back(0xe1);
        m_buf.push_back(0x00);
        int esInfoLength = ((table[m_videoDescPos + 3] & 0x03) << 8) | table[m_videoDescPos + 4];
        m_buf.insert(m_buf.end(), table + m_videoDescPos + 3, table + m_videoDescPos + 5 + esInfoLength);
        if (m_pcrPid == m_videoPid) {
            m_buf[9] = 0xe1;
            m_buf[10] = 0x00;
        }
    }
    if (m_audio1Pid != 0 || m_audio1Mode == 1) {
        m_buf.push_back(ADTS_TRANSPORT);
        // PID=0x0110
        m_buf.push_back(0xe1);
        m_buf.push_back(0x10);
        if (m_audio1Pid != 0) {
            int esInfoLength = ((table[m_audio1DescPos + 3] & 0x03) << 8) | table[m_audio1DescPos + 4];
            if (m_audio1ComponentTagUnknown && (m_audio2Pid != 0 || m_audio2Mode == 1)) {
                int esInfoNewLength = esInfoLength + 3;
                m_buf.push_back(0xf0 | static_cast<uint8_t>(esInfoNewLength >> 8));
                m_buf.push_back( static_cast<uint8_t>(esInfoNewLength));
                m_buf.push_back(0x52);
                m_buf.push_back(1);
                m_buf.push_back(maybeCProfile ? 0x83 : 0x10);
            }
            else {
                m_buf.push_back(0xf0 | static_cast<uint8_t>(esInfoLength >> 8));
                m_buf.push_back( static_cast<uint8_t>(esInfoLength));
            }
            m_buf.insert(m_buf.end(), table + m_audio1DescPos + 5, table + m_audio1DescPos + 5 + esInfoLength);
            if (m_pcrPid == m_audio1Pid) {
                m_buf[9] = 0xe1;
                m_buf[10] = 0x10;
            }
        }
        else {
            m_buf.push_back(0xf0);
            m_buf.push_back(3);
            m_buf.push_back(0x52);
            m_buf.push_back(1);
            m_buf.push_back(maybeCProfile ? 0x83 : 0x10);
        }
    }
    if (m_audio2Pid != 0 || m_audio2Mode == 1) {
        m_buf.push_back(ADTS_TRANSPORT);
        // PID=0x0111
        m_buf.push_back(0xe1);
        m_buf.push_back(0x11);
        if (m_audio2Pid != 0) {
            int esInfoLength = ((table[m_audio2DescPos + 3] & 0x03) << 8) | table[m_audio2DescPos + 4];
            m_buf.insert(m_buf.end(), table + m_audio2DescPos + 3, table + m_audio2DescPos + 5 + esInfoLength);
            if (m_pcrPid == m_audio2Pid) {
                m_buf[9] = 0xe1;
                m_buf[10] = 0x11;
            }
        }
        else {
            m_buf.push_back(0xf0);
            m_buf.push_back(3);
            m_buf.push_back(0x52);
            m_buf.push_back(1);
            m_buf.push_back(maybeCProfile ? 0x85 : 0x11);
        }
    }
    if (m_captionPid != 0 || m_captionMode == 1) {
        m_buf.push_back(PES_PRIVATE_DATA);
        // PID=0x0130
        m_buf.push_back(0xe1);
        m_buf.push_back(0x30);
        if (m_captionPid != 0) {
            int esInfoLength = ((table[m_captionDescPos + 3] & 0x03) << 8) | table[m_captionDescPos + 4];
            m_buf.insert(m_buf.end(), table + m_captionDescPos + 3, table + m_captionDescPos + 5 + esInfoLength);
            if (m_pcrPid == m_captionPid) {
                m_buf[9] = 0xe1;
                m_buf[10] = 0x30;
            }
        }
        else {
            m_buf.push_back(0xf0);
            m_buf.push_back(3 + (maybeCProfile ? 0 : 5));
            m_buf.push_back(0x52);
            m_buf.push_back(1);
            m_buf.push_back(maybeCProfile ? 0x87 : 0x30);
            if (!maybeCProfile) {
                // data_component_descriptor
                m_buf.push_back(0xfd);
                m_buf.push_back(3);
                m_buf.push_back(0x00);
                m_buf.push_back(0x08);
                m_buf.push_back(0x3d);
            }
        }
    }
    if (m_superimposePid != 0 || m_superimposeMode == 1) {
        m_buf.push_back(PES_PRIVATE_DATA);
        // PID=0x0138
        m_buf.push_back(0xe1);
        m_buf.push_back(0x38);
        if (m_superimposePid != 0) {
            int esInfoLength = ((table[m_superimposeDescPos + 3] & 0x03) << 8) | table[m_superimposeDescPos + 4];
            m_buf.insert(m_buf.end(), table + m_superimposeDescPos + 3, table + m_superimposeDescPos + 5 + esInfoLength);
            if (m_pcrPid == m_superimposePid) {
                m_buf[9] = 0xe1;
                m_buf[10] = 0x38;
            }
        }
        else {
            m_buf.push_back(0xf0);
            m_buf.push_back(3 + (maybeCProfile ? 0 : 5));
            // component_tag=0x38
            m_buf.push_back(0x52);
            m_buf.push_back(1);
            m_buf.push_back(maybeCProfile ? 0x88 : 0x38);
            if (!maybeCProfile) {
                // data_component_descriptor
                m_buf.push_back(0xfd);
                m_buf.push_back(3);
                m_buf.push_back(0x00);
                m_buf.push_back(0x08);
                m_buf.push_back(0x3c);
            }
        }
    }

    m_buf[2] = 0xb0 | static_cast<uint8_t>((m_buf.size() + 4 - 4) >> 8);
    m_buf[3] = static_cast<uint8_t>(m_buf.size() + 4 - 4);

    if (m_lastPmt.size() == m_buf.size() + 4 &&
        std::equal(m_buf.begin(), m_buf.end(), m_lastPmt.begin())) {
        // Copy CRC
        m_buf.insert(m_buf.end(), m_lastPmt.end() - 4, m_lastPmt.end());
    }
    else {
        // Increment version number
        m_buf[6] = 0xc1 | (((m_buf[6] >> 1) + 1) & 0x1f) << 1;
        uint32_t crc = calc_crc32(m_buf.data() + 1, static_cast<int>(m_buf.size() - 1));
        m_buf.push_back(crc >> 24);
        m_buf.push_back((crc >> 16) & 0xff);
        m_buf.push_back((crc >> 8) & 0xff);
        m_buf.push_back(crc & 0xff);
        m_lastPmt = m_buf;
    }

    // Create TS packets
    for (size_t i = 0; i < m_buf.size(); i += 184) {
        m_packets.push_back(0x47);
        // PMT_PID=0x01f0
        m_packets.push_back((i == 0 ? 0x40 : 0) | 0x01);
        m_packets.push_back(0xf0);
        m_pmtCounter = (m_pmtCounter + 1) & 0x0f;
        m_packets.push_back(0x10 | m_pmtCounter);
        m_packets.insert(m_packets.end(), m_buf.begin() + i, m_buf.begin() + std::min(i + 184, m_buf.size()));
        m_packets.resize(((m_packets.size() - 1) / 188 + 1) * 188, 0xff);
    }
}

void CServiceFilter::AddPcrAdaptation(const uint8_t *pcr)
{
    // Create TS packet
    m_packets.push_back(0x47);
    // PCR_PID=0x01ff
    m_packets.push_back(0x01);
    m_packets.push_back(0xff);
    m_packets.push_back(0x20);
    m_packets.push_back(183);
    m_packets.push_back(0x10);
    m_packets.insert(m_packets.end(), pcr, pcr + 4);
    // pcr_extension=0
    m_packets.push_back((pcr[4] & 0x80) | 0x7e);
    m_packets.push_back(0);
    m_packets.resize((m_packets.size() / 188 + 1) * 188, 0xff);
}

void CServiceFilter::ChangePidAndAddPacket(const uint8_t *packet, int pid, uint8_t counter)
{
    m_packets.push_back(0x47);
    m_packets.push_back((packet[1] & 0xe0) | static_cast<uint8_t>(pid >> 8));
    m_packets.push_back(static_cast<uint8_t>(pid));
    m_packets.push_back(counter > 0x0f ? packet[3] : ((packet[3] & 0xf0) | counter));
    m_packets.insert(m_packets.end(), packet + 4, packet + 188);
}

void CServiceFilter::AddAudioPesPackets(uint8_t index, int64_t targetPts, int64_t &pts, uint8_t &counter)
{
    static const int ACCEPTABLE_PTS_DIFF_SEC = 10;

    int64_t ptsDiff = (0x200000000 + targetPts - pts) & 0x1ffffffff;
    if (pts < 0 || (90000 * ACCEPTABLE_PTS_DIFF_SEC < ptsDiff && ptsDiff < 0x200000000 - 90000 * ACCEPTABLE_PTS_DIFF_SEC)) {
        pts = targetPts;
    }
    for (;;) {
        int64_t nextPts = (pts + 90000 * 64 / 1000) & 0x1ffffffff;
        if (((0x200000000 + targetPts - nextPts) & 0x1ffffffff) > 900000) {
            break;
        }
        Add64MsecAudioPesPacket(index, pts, counter);
        pts = nextPts;
    }
}

void CServiceFilter::Add64MsecAudioPesPacket(uint8_t index, int64_t pts, uint8_t &counter)
{
    static const uint8_t ADTS_2CH_48KHZ_SILENT[13] = {
        0xff, 0xf1, 0x4c, 0x80, 0x01, 0xbf, 0xfc, 0x21, 0x10, 0x04, 0x60, 0x8c, 0x1c
    };
    m_packets.push_back(0x47);
    // PID=0x0110+index
    m_packets.push_back(0x41);
    m_packets.push_back(0x10 | index);
    counter = (counter + 1) & 0x0f;
    m_packets.push_back(0x30 | counter);
    m_packets.push_back(188 - 5 - (6 + 8 + 13 * 3));
    m_packets.push_back(0x40);
    // stuffing
    m_packets.resize(m_packets.size() + 188 - 6 - (6 + 8 + 13 * 3), 0xff);
    // PES
    m_packets.push_back(0);
    m_packets.push_back(0);
    m_packets.push_back(1);
    m_packets.push_back(0xc0 | index);
    m_packets.push_back(0);
    m_packets.push_back(8 + 13 * 3);
    // alignment by audio sync word
    m_packets.push_back(0x84);
    // has PTS
    m_packets.push_back(0x80);
    m_packets.push_back(5);
    m_packets.push_back(static_cast<uint8_t>(pts >> 29) | 0x21); // 3 bits
    m_packets.push_back(static_cast<uint8_t>(pts >> 22)); // 8 bits
    m_packets.push_back(static_cast<uint8_t>(pts >> 14) | 1); // 7 bits
    m_packets.push_back(static_cast<uint8_t>(pts >> 7)); // 8 bits
    m_packets.push_back(static_cast<uint8_t>(pts << 1) | 1); // 7 bits
    // 1024samples(1frame) / 48000hz * 3 = 0.064sec
    for (int i = 0; i < 3; ++i) {
        m_packets.insert(m_packets.end(), ADTS_2CH_48KHZ_SILENT, ADTS_2CH_48KHZ_SILENT + 13);
    }
}

int64_t CServiceFilter::GetAudioPresentationTimeStamp(int unitStart, const uint8_t *payload, int payloadSize)
{
    if (unitStart && payloadSize >= 6 && payload[0] == 0 && payload[1] == 0 && payload[2] == 1) {
        int streamID = payload[3];
        size_t pesPacketLength = (payload[4] << 8) | payload[5];
        // audio stream
        if ((streamID & 0xe0) == 0xc0 && pesPacketLength >= 3 && payloadSize >= 9) {
            int ptsDtsFlags = payload[7] >> 6;
            if (ptsDtsFlags >= 2 && pesPacketLength >= 8 && payloadSize >= 14) {
                return (payload[13] >> 1) |
                       (payload[12] << 7) |
                       ((payload[11] & 0xfe) << 14) |
                       (payload[10] << 22) |
                       (static_cast<int64_t>(payload[9] & 0x0e) << 29);
            }
        }
    }
    return -1;
}
