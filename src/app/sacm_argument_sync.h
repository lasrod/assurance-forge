#pragma once

namespace parser {
struct AssuranceCase;
}

namespace sacm {
struct AssuranceCasePackage;
}

namespace app {

void RebuildSacmArgumentPackageFromParser(const parser::AssuranceCase& model, sacm::AssuranceCasePackage& package);

} // namespace app
