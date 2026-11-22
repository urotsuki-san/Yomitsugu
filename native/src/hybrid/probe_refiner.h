#pragma once
#include "ime_engine.h"
#include "iroha/zenz_engine.h"
#include <memory>

class ProbeRefiner {
public:
    ProbeRefiner();
    void Apply(const ime::DecodeInput&, std::vector<ime::Candidate>*);
private:
    std::string path_;
    bool prefer_ = false;
    bool diverse_ = false;
    std::unique_ptr<iroha::ZenzEngine> model_;
};
