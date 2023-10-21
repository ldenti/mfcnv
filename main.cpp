#include <iostream>
#include <map>
#include <numeric>
#include <string>
#include <zlib.h>

#include <kmc_api/kmc_file.h>
#include <kseq.h>

KSEQ_INIT(gzFile, gzread)

static const uint8_t to_int[128] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 0
                                    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 10
                                    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 20
                                    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 30
                                    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 40
                                    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 50
                                    0, 0, 0, 0, 0, 1, 0, 2, 0, 0, // 60
                                    0, 3, 0, 0, 0, 0, 0, 0, 0, 0, // 70
                                    0, 0, 0, 0, 4, 0, 0, 0, 0, 0, // 80
                                    0, 0, 0, 0, 0, 0, 0, 1, 0, 2, // 90
                                    0, 0, 0, 3, 0, 0, 0, 0, 0, 0, // 100
                                    0, 0, 0, 0, 0, 0, 4, 0, 0, 0, // 110
                                    0, 0, 0, 0, 0, 0, 0, 0};      // 120

inline uint8_t reverse_char(const uint8_t c) { return ((~c) & 3); }

uint64_t revcompl(uint64_t kmer, const uint8_t k) {
  uint64_t rckmer = 0;
  kmer = ~kmer;
  for (uint i = 0; i < k; ++i) {
    rckmer = (rckmer << 2) | (kmer & 3);
    kmer >>= 2;
  }
  return rckmer;
}

inline uint64_t lsappend(const uint64_t kmer, const uint64_t c,
                         const uint64_t k) { // left shift and append
  return ((kmer << 2) | c) & ((1UL << 2 * k) - 1);
}

inline uint64_t rsprepend(const uint64_t kmer, const uint64_t c,
                          const uint64_t k) { // right shift and prepend
  return (kmer >> 2) | (c << (2 * k - 2));
}

uint64_t kmer2d(char *kmer, uint32 k) {
  uint64_t kmer_d = 0;
  for (uint i = 0; i < k; ++i)
    kmer_d = (kmer_d << 2) | (to_int[kmer[i]] - 1);
  // std::cerr << kmer << " " << kmer_d << " " << counter << std::endl;
  return kmer_d;
}

std::map<uint64_t, uint8_t> extract_kmers_from_db(char *kmc_path,
                                                  uint32_t *klen) {
  std::map<uint64_t, uint8_t> KMERS;
  CKMCFile kmer_db;
  if (!kmer_db.OpenForListing(kmc_path)) {
    std::cerr << "ERROR: cannot open " << kmc_path << std::endl;
    exit(1);
  }
  uint32 mode, min_counter, pref_len, sign_len, min_c, counter;
  uint64 tot_kmers, max_c;
  kmer_db.Info(*klen, mode, min_counter, pref_len, sign_len, min_c, max_c,
               tot_kmers);
  CKmerAPI kmer_obj(*klen);
  char kmer[*klen + 1];
  std::cerr << "Parsing KMC database.." << std::endl;
  while (kmer_db.ReadNextKmer(kmer_obj, counter)) {
    kmer_obj.to_string(kmer);
    KMERS[kmer2d(kmer, *klen)] = counter;
  }
  return KMERS;
}

int main(int argc, char *argv[]) {
  char *fa_path = argv[1];
  char *kcontrol_path = argv[2];
  char *kcase_path = argv[3];
  uint wsize = std::stoi(argv[4]);

  uint32_t klen;
  std::map<uint64_t, uint8_t> KMERS_CONTROL =
      extract_kmers_from_db(kcontrol_path, &klen);
  std::cerr << "Extracted " << KMERS_CONTROL.size() << " kmers from control db"
            << std::endl;
  std::map<uint64_t, uint8_t> KMERS_CASE =
      extract_kmers_from_db(kcase_path, &klen);
  std::cerr << "Extracted " << KMERS_CASE.size() << " kmers from case db"
            << std::endl;
  gzFile fa = gzopen(fa_path, "r");
  kseq_t *seq = kseq_init(fa);
  char kmer[klen + 1];   // first kmer on sequence (plain)
  int l;                 // chromosome size
  uint64_t kmer_d = 0;   // kmer
  uint64_t rckmer_d = 0; // reverse and complemented kmer
  uint64_t ckmer_d = 0;  // canonical kmer
  uint8_t c;             // new character to append
  uint w = 0;            // weight
  int p = 0;             // current position on chromosome
  int bin_p = 0;         // current position in bin
  std::cerr << "Reading reference.." << std::endl;

  std::vector<uint> control_bin(wsize);
  std::vector<uint> case_bin(wsize);
  std::vector<float> ratio_bin(wsize);

  int tot_l = 0;
  while ((l = kseq_read(seq)) >= 0)
    tot_l += l - klen;

  float case_norm =
      (float)std::accumulate(
          KMERS_CASE.begin(), KMERS_CASE.end(), 0,
          [](const int prev_sum, const std::pair<uint64_t, uint8_t> &entry) {
            return prev_sum + entry.second;
          }) /
      tot_l;
  float control_norm =
      (float)std::accumulate(
          KMERS_CONTROL.begin(), KMERS_CONTROL.end(), 0,
          [](const int prev_sum, const std::pair<uint64_t, uint8_t> &entry) {
            return prev_sum + entry.second;
          }) /
      tot_l;
  std::cerr << case_norm << " " << control_norm << std::endl;
  // reinit fasta reader
  gzclose(fa);
  fa = gzopen(fa_path, "r");
  seq = kseq_init(fa);
  while ((l = kseq_read(seq)) >= 0) {
    // std::cerr << "\t`> " << seq->name.s << std::endl;

    strncpy(kmer, seq->seq.s, klen);
    kmer_d = kmer2d(kmer, klen);
    rckmer_d = revcompl(kmer_d, klen);
    ckmer_d = std::min(kmer_d, rckmer_d);

    control_bin[bin_p] = KMERS_CONTROL.find(ckmer_d) != KMERS_CONTROL.end()
                             ? KMERS_CONTROL.at(ckmer_d)
                             : 0;
    case_bin[bin_p] = KMERS_CASE.find(ckmer_d) != KMERS_CASE.end()
                          ? KMERS_CASE.at(ckmer_d)
                          : 0;
    // std::cerr << seq->name.s << "\t" << p + 1 << "\t" << control_bin[bin_p]
    //           << "\t" << case_bin[bin_p] << std::endl;
    ++bin_p;

    for (p = klen; p < l; ++p) {
      c = to_int[seq->seq.s[p]] - 1; // A is 1 but it should be 0
      kmer_d = lsappend(kmer_d, c, klen);
      rckmer_d = rsprepend(rckmer_d, reverse_char(c), klen);
      ckmer_d = std::min(kmer_d, rckmer_d);

      control_bin[bin_p] = KMERS_CONTROL.find(ckmer_d) != KMERS_CONTROL.end()
                               ? KMERS_CONTROL.at(ckmer_d)
                               : 0;
      case_bin[bin_p] = KMERS_CASE.find(ckmer_d) != KMERS_CASE.end()
                            ? KMERS_CASE.at(ckmer_d)
                            : 0;
      ratio_bin[bin_p] =
          (float)(case_bin[bin_p] == 0 ? 0.0001 : case_bin[bin_p] / case_norm) /
          (float)(control_bin[bin_p] == 0 ? 0.0001
                                          : control_bin[bin_p] / control_norm);
      // std::cerr << seq->name.s << "\t" << p - klen + 2 << "\t"
      //           << case_bin[bin_p] << "\t" << control_bin[bin_p] << "\t"
      //           << ratio_bin[bin_p] << std::endl;
      ++bin_p;
      if (bin_p >= wsize) {
        std::cout << seq->name.s << ":" << p - klen + 2 - wsize << "-"
                  << p - klen + 1 << "\t";
        std::cout << case_bin[0];
        for (int _ = 1; _ < wsize; ++_)
          std::cout << "," << case_bin[_] / case_norm;
        std::cout << "\t";
        std::cout << control_bin[0];
        for (int _ = 1; _ < wsize; ++_)
          std::cout << "," << control_bin[_] / control_norm;
        std::cout << "\t";

        std::cout << ratio_bin[0];
        for (int _ = 1; _ < wsize; ++_)
          std::cout << "," << ratio_bin[_];
        std::cout << "\t";
        size_t n = wsize / 2; // CHECKME: what about odd wsize?
        nth_element(ratio_bin.begin(), ratio_bin.begin() + n, ratio_bin.end());
        std::cout << ratio_bin[n];
        bin_p = 0;
        std::cout << std::endl;
      }
    }
    // TODO: manage last k-1 bases
    // while (p - klen < l - 1) {
    //   std::cout << seq->name.s << "\t" << p - klen + 2 << "\t" << 0
    //             << std::endl;
    //   ++p;
    // }
  }

  kseq_destroy(seq);
  gzclose(fa);
  return 0;
}