#include "ir_builder.h"
#include "ir_type.h"
#include "rewind_ir.h"
#include "symbol_table.h"
#include <string>
#include <utility>
#include <vector>

namespace rewind_ir
{
namespace
{

inline const IRInt32Type* get_i32_type()
{
    return IRTypeContext::instance().getInt32();
}

inline const IRUnitType* get_unit_type()
{
    return IRTypeContext::instance().getUnit();
}

inline const IRPointerType* get_pointer_type(const IRType* base_type)
{
    return IRTypeContext::instance().getPointer(base_type);
}

inline const IRFunctionType* get_function_type(std::vector<const IRType*> params, const IRType* ret)
{
    return IRTypeContext::instance().getFunction(std::move(params), ret);
}

IRFunction* declare_external_function(IRModule& module,
                                      SymbolTable& module_symbols,
                                      const std::string& name,
                                      std::vector<const IRType*> param_types,
                                      const IRType* return_type)
{
    auto* function_type = get_function_type(param_types, return_type);
    auto* func = module.make_function(function_type, name, true);
    module_symbols.define_function(name, func);

    for (size_t i = 0; i < param_types.size(); ++i) {
        auto* arg_ref = module.make_value<IRFuncArgRef>(i, param_types[i]);
        module.append_param(*func, *arg_ref);
    }

    return func;
}

} // namespace

void RewindIRBuilder::declare_library_function(IRModule& module)
{
    const auto* i32 = get_i32_type();
    const auto* unit = get_unit_type();
    const auto* i32_ptr = get_pointer_type(i32);

    declare_external_function(module, module_symbols_, "getint", {}, i32);
    declare_external_function(module, module_symbols_, "getch", {}, i32);
    declare_external_function(module, module_symbols_, "putint", {i32}, unit);
    declare_external_function(module, module_symbols_, "putch", {i32}, unit);
    declare_external_function(module, module_symbols_, "starttime", {}, unit);
    declare_external_function(module, module_symbols_, "stoptime", {}, unit);

    declare_external_function(module, module_symbols_, "getarray", {i32_ptr}, i32);
    declare_external_function(module, module_symbols_, "putarray", {i32, i32_ptr}, unit);
}

} // namespace rewind_ir
