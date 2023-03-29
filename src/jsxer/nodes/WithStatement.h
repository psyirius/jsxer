#pragma once

#include "AstNode.h"
#include "../decoders.h"

namespace jsxer::nodes {
    class WithStatement : public AstNode {
    public:
        DEFINE_NODE_TYPE(WithStatement);

        explicit WithStatement(Reader& reader) : AstNode(reader) {}

        void parse() override;

        string to_string() override;

    private:
        decoders::LineInfo bodyInfo;
        AstOpNode object;
    };
}
