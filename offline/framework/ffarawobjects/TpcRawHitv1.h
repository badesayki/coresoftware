#ifndef FUN4ALLRAW_TPCRAWTHITV1_H
#define FUN4ALLRAW_TPCRAWTHITV1_H

#include "TpcRawHit.h"

#include <phool/PHObject.h>

#include <cassert>
#include <limits>

class TpcRawHitv1 : public TpcRawHit
{
 public:
  TpcRawHitv1() = default;
  TpcRawHitv1(TpcRawHit *tpchit);
  ~TpcRawHitv1() override = default;

  /** identify Function from PHObject
      @param os Output Stream
   */
  void identify(std::ostream &os = std::cout) const override;

  void Clear(Option_t *) override;

  uint64_t get_bco() const override { return bco; }
  // cppcheck-suppress virtualCallInConstructor
  void set_bco(const uint64_t val) override { bco = val; }

  uint64_t get_gtm_bco() const override { return gtm_bco; }
  // cppcheck-suppress virtualCallInConstructor
  void set_gtm_bco(const uint64_t val) override { gtm_bco = val; }

  int32_t get_packetid() const override { return packetid; }
  // cppcheck-suppress virtualCallInConstructor
  void set_packetid(const int32_t val) override { packetid = val; }

  uint16_t get_fee() const override { return fee; }
  // cppcheck-suppress virtualCallInConstructor
  void set_fee(const uint16_t val) override { fee = val; }

  uint16_t get_channel() const override { return channel; }
  // cppcheck-suppress virtualCallInConstructor
  void set_channel(const uint16_t val) override { channel = val; }

  uint16_t get_sampaaddress() const override { return sampaaddress; }
  // cppcheck-suppress virtualCallInConstructor
  void set_sampaaddress(const uint16_t val) override { sampaaddress = val; }

  uint16_t get_sampachannel() const override { return sampachannel; }
  // cppcheck-suppress virtualCallInConstructor
  void set_sampachannel(const uint16_t val) override { sampachannel = val; }

  uint16_t get_samples() const override { return samples; }
  // cppcheck-suppress virtualCallInConstructor
  void set_samples(const uint16_t val) override
  {
    // assign
    samples = val;

    // resize adc vector
    adc.resize(val, 0);
  }

  uint16_t get_adc(uint16_t sample) const override
  {
    assert(sample < adc.size());
    return adc[sample];
  }

  // cppcheck-suppress virtualCallInConstructor
  void set_adc(uint16_t sample, uint16_t val) override
  {
    assert(sample < adc.size());
    adc[sample] = val;
  }

 private:
  uint64_t bco = std::numeric_limits<uint64_t>::max();
  uint64_t gtm_bco = std::numeric_limits<uint64_t>::max();
  int32_t packetid = std::numeric_limits<int32_t>::max();
  uint16_t fee = std::numeric_limits<uint16_t>::max();
  uint16_t channel = std::numeric_limits<uint16_t>::max();
  uint16_t sampaaddress = std::numeric_limits<uint16_t>::max();
  uint16_t sampachannel = std::numeric_limits<uint16_t>::max();
  uint16_t samples = std::numeric_limits<uint16_t>::max();

  //! adc value for each sample
  std::vector<uint16_t> adc;

  ClassDefOverride(TpcRawHitv1, 1)
};

#endif
