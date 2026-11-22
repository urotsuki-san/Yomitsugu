#include "iroha/zenz_engine.h"
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>

int wmain(int argc, wchar_t** argv) {
    if (argc!=2) return 2;
    _putenv_s("IROHA_THREADS","4");
    _putenv_s("IROHA_GPU_LAYERS","0");
    _putenv_s("IROHA_CROSS_KV","1");
    const std::u32string reading=U"きょうはいいてんきですね";
    iroha::ZenzEngine engine(std::filesystem::path(argv[1]).u8string());
    std::string error;
    std::vector<std::u32string> gold, observed;
    if (!engine.PrewarmForInput(reading,U"",&error) ||
        !engine.Convert(reading,U"",1,&gold,&error) || gold.empty() || gold[0].size()<6) return 3;
    auto changed=gold[0];
    changed[3]=U'鰯';
    iroha::DraftMeasure repaired;
    if (!engine.ConvertGuided(reading,U"",changed,4,repaired,&observed,&error) || observed!=gold ||
        !repaired.partial || !repaired.confirmed) return 4;
    if (!engine.Convert(reading,U"",1,&observed,&error) || observed!=gold) return 5;
    iroha::DraftPolicy policy;
    for (int i=0;i<5;++i) {
        const int width=policy.Width(0,false);
        if ((width>0)!=(i<2 || i==4)) return 6;
        if (width) {
            iroha::DraftMeasure m;
            const std::u32string draft=i==4 ? gold[0] : U"鰯鰯鰯鰯鰯鰯鰯鰯鰯鰯鰯";
            if (!engine.ConvertGuided(reading,U"",draft,width,m,&observed,&error) || observed!=gold) return 7;
            if (i==4 && !m.confirmed) return 8;
            policy.Observe(0,m);
        } else if (!engine.Convert(reading,U"",1,&observed,&error) || observed!=gold) return 9;
    }
    // Force divergent endings, an exact duplicate, a shorter prefix, an empty
    // candidate and a reordered batch. A separate one-candidate call is the reference.
    std::vector<std::u32string> surfaces{gold[0], gold[0]+U"。", gold[0]+U"ね", U"", gold[0], gold[0].substr(0,4)};
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<float> grouped;
        if (!engine.Score(surfaces,reading,U"",&grouped,&error) || grouped.size()!=surfaces.size()) return 10;
        for (size_t i = 0; i < surfaces.size(); ++i) {
            std::vector<float> scalar;
            if (!engine.Score({surfaces[i]},reading,U"",&scalar,&error) || scalar.size()!=1) return 11;
            if (std::isnan(scalar[0]) || std::isnan(grouped[i])) return 12;
            if (scalar[0] != grouped[i] && (!std::isfinite(scalar[0]) || !std::isfinite(grouped[i]) ||
                std::abs(scalar[0] - grouped[i]) > 0.02f)) return 13;
        }
        std::reverse(surfaces.begin(),surfaces.end());
    }
    std::cout<<"PASS real iroha output after a partial block, reused prompt, two missed drafts, two serial requests and a successful reprobe\n";
}
