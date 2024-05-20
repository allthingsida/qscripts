#include "idasdk.h"

#include <sstream>
#include <triton/context.hpp>
#include <triton/basicBlock.hpp>
#include <triton/x86Specifications.hpp>

using namespace triton;
using namespace triton::arch;

bool main(size_t)
{
    constexpr const ea_t FUNC_EA = 0x400000;
    msg_clear();

    auto f = get_func(FUNC_EA);
    if (f == nullptr)
    {
        msg("Can't find test function @ %a!\n", FUNC_EA);
        return false;
    }

    /* Init the triton context */
    triton::Context ctx;
    ctx.setArchitecture(ARCH_X86_64);

    func_item_iterator_t ffi;
    BasicBlock bb;
    for (bool ok = ffi.set(f); ok; ok = ffi.next_code())
    {
        auto ea = ffi.current();
        insn_t insn;
        if (decode_insn(&insn, ea) == 0)
        {
            msg("Failed to decode at %a\n", ea);
            return false;
        }

        unsigned char buf[32];
        get_bytes(buf, qmin(sizeof(buf), insn.size), ea);

        // Setup opcode
        Instruction inst;
        inst.setOpcode(buf, insn.size);
        inst.setAddress(triton::uint64(ea));
        bb.add(inst);
    }

    ctx.disassembly(bb, FUNC_EA);

    std::ostringstream ostr;
    ostr << bb;
    msg("----------------\n"
        "Original:\n"
        "----------------\n"
        "%s\n", ostr.str().c_str());

    ostr.str("");
    auto simplified_bb = ctx.simplify(bb);
    ostr << simplified_bb;
    msg("----------------\n"
        "Simplified:\n"
        "----------------\n"
        "%s\n", ostr.str().c_str());

	return true;
}
