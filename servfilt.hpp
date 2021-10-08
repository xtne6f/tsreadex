#ifndef INCLUDE_SERVFILT_HPP
#define INCLUDE_SERVFILT_HPP

#include "util.hpp"
#include <stdint.h>
#include <vector>

class CServiceFilter
{
public:
    CServiceFilter();
    void SetProgramNumberOrIndex(int n) { m_programNumberOrIndex = n; }
    void SetAudio1Mode(int mode) { m_audio1Mode = mode; }
    void SetAudio2Mode(int mode) { m_audio2Mode = mode; }
    void SetCaptionMode(int mode) { m_captionMode = mode; }
    void SetSuperimposeMode(int mode) { m_superimposeMode = mode; }
    void AddPacket(const uint8_t *packet);
    const std::vector<uint8_t> &GetPackets() const { return m_packets; }
    void ClearPackets() { m_packets.clear(); }

private:
    static std::vector<PMT_REF>::const_iterator FindNitRef(const std::vector<PMT_REF> &pmt);
    std::vector<PMT_REF>::const_iterator FindTargetPmtRef(const std::vector<PMT_REF> &pmt) const;
    void AddPat(int transportStreamID, int programNumber, bool addNit);
    void AddPmt(const PSI &psi);
    void AddPcrAdaptation(const uint8_t *pcr);
    void ChangePidAndAddPacket(const uint8_t *packet, int pid, uint8_t counter = 0xff);
    void AddAudioPesPackets(uint8_t index, int64_t targetPts, int64_t &pts, uint8_t &counter);
    void Add64MsecAudioPesPacket(uint8_t index, int64_t pts, uint8_t &counter);
    static int64_t GetAudioPresentationTimeStamp(int unitStart, const uint8_t *payload, int payloadSize);

    int m_programNumberOrIndex;
    int m_audio1Mode;
    int m_audio2Mode;
    int m_captionMode;
    int m_superimposeMode;
    std::vector<uint8_t> m_packets;
    PAT m_pat;
    PSI m_pmtPsi;
    int m_videoPid;
    int m_audio1Pid;
    int m_audio2Pid;
    int m_captionPid;
    int m_superimposePid;
    int m_pcrPid;
    int64_t m_pcr;
    uint8_t m_patCounter;
    uint8_t m_pmtCounter;
    uint8_t m_audio1PesCounter;
    uint8_t m_audio2PesCounter;
    int m_audio1PesCounterBase;
    int m_audio2PesCounterBase;
    int64_t m_audio1Pts;
    int64_t m_audio2Pts;
    int64_t m_audio1PtsPcrDiff;
    int64_t m_audio2PtsPcrDiff;
    std::vector<uint8_t> m_buf;
    std::vector<uint8_t> m_lastPat;
    std::vector<uint8_t> m_lastPmt;
};

#endif
