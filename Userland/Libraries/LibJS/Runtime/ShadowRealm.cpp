/*
 * Copyright (c) 2021-2022, Linus Groh <linusg@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Lexer.h>
#include <LibJS/Parser.h>
#include <LibJS/Runtime/AbstractOperations.h>
#include <LibJS/Runtime/DeclarativeEnvironment.h>
#include <LibJS/Runtime/ModuleNamespaceObject.h>
#include <LibJS/Runtime/NativeFunction.h>
#include <LibJS/Runtime/PromiseConstructor.h>
#include <LibJS/Runtime/PromiseReaction.h>
#include <LibJS/Runtime/ShadowRealm.h>
#include <LibJS/Runtime/WrappedFunction.h>

namespace JS {

ShadowRealm::ShadowRealm(Realm& shadow_realm, ExecutionContext execution_context, Object& prototype)
    : Object(prototype)
    , m_shadow_realm(shadow_realm)
    , m_execution_context(move(execution_context))
{
}

void ShadowRealm::visit_edges(Visitor& visitor)
{
    Base::visit_edges(visitor);

    visitor.visit(&m_shadow_realm);
}

// 3.1.1 PerformShadowRealmEval ( sourceText, callerRealm, evalRealm ), https://tc39.es/proposal-shadowrealm/#sec-performshadowrealmeval
ThrowCompletionOr<Value> perform_shadow_realm_eval(GlobalObject& global_object, StringView source_text, Realm& caller_realm, Realm& eval_realm)
{
    auto& vm = global_object.vm();

    // 1. Assert: Type(sourceText) is String.
    // 2. Assert: callerRealm is a Realm Record.
    // 3. Assert: evalRealm is a Realm Record.

    // 4. Perform ? HostEnsureCanCompileStrings(callerRealm, evalRealm).
    // FIXME: We don't have this host-defined abstract operation yet.

    // 5. Perform the following substeps in an implementation-defined order, possibly interleaving parsing and error detection:

    // a. Let script be ParseText(! StringToCodePoints(sourceText), Script).
    auto parser = Parser(Lexer(source_text));
    auto program = parser.parse_program();

    // b. If script is a List of errors, throw a SyntaxError exception.
    if (parser.has_errors()) {
        auto& error = parser.errors()[0];
        return vm.throw_completion<JS::SyntaxError>(global_object, error.to_string());
    }

    // c. If script Contains ScriptBody is false, return undefined.
    if (program->children().is_empty())
        return js_undefined();

    // d. Let body be the ScriptBody of script.
    // e. If body Contains NewTarget is true, throw a SyntaxError exception.
    // f. If body Contains SuperProperty is true, throw a SyntaxError exception.
    // g. If body Contains SuperCall is true, throw a SyntaxError exception.
    // FIXME: Implement these, we probably need a generic way of scanning the AST for certain nodes.

    // 6. Let strictEval be IsStrict of script.
    auto strict_eval = program->is_strict_mode();

    // 7. Let runningContext be the running execution context.
    // NOTE: This would be unused due to step 11 and is omitted for that reason.

    // 8. Let lexEnv be NewDeclarativeEnvironment(evalRealm.[[GlobalEnv]]).
    Environment* lexical_environment = new_declarative_environment(eval_realm.global_environment());

    // 9. Let varEnv be evalRealm.[[GlobalEnv]].
    Environment* variable_environment = &eval_realm.global_environment();

    // 10. If strictEval is true, set varEnv to lexEnv.
    if (strict_eval)
        variable_environment = lexical_environment;

    // 11. If runningContext is not already suspended, suspend runningContext.
    // NOTE: We don't support this concept yet.

    // 12. Let evalContext be a new ECMAScript code execution context.
    auto eval_context = ExecutionContext { vm.heap() };

    // 13. Set evalContext's Function to null.
    eval_context.function = nullptr;

    // 14. Set evalContext's Realm to evalRealm.
    eval_context.realm = &eval_realm;

    // 15. Set evalContext's ScriptOrModule to null.
    // Note: This is already the default value.

    // 16. Set evalContext's VariableEnvironment to varEnv.
    eval_context.variable_environment = variable_environment;

    // 17. Set evalContext's LexicalEnvironment to lexEnv.
    eval_context.lexical_environment = lexical_environment;

    // Non-standard
    eval_context.is_strict_mode = strict_eval;

    // 18. Push evalContext onto the execution context stack; evalContext is now the running execution context.
    TRY(vm.push_execution_context(eval_context, eval_realm.global_object()));

    // 19. Let result be EvalDeclarationInstantiation(body, varEnv, lexEnv, null, strictEval).
    auto eval_result = eval_declaration_instantiation(vm, eval_realm.global_object(), program, variable_environment, lexical_environment, nullptr, strict_eval);

    Completion result;

    // 20. If result.[[Type]] is normal, then
    if (!eval_result.is_throw_completion()) {
        // TODO: Optionally use bytecode interpreter?
        // a. Set result to the result of evaluating body.
        result = program->execute(vm.interpreter(), eval_realm.global_object());
    }

    // 21. If result.[[Type]] is normal and result.[[Value]] is empty, then
    if (result.type() == Completion::Type::Normal && !result.value().has_value()) {
        // a. Set result to NormalCompletion(undefined).
        result = normal_completion(js_undefined());
    }

    // 22. Suspend evalContext and remove it from the execution context stack.
    // NOTE: We don't support this concept yet.
    vm.pop_execution_context();

    // 23. Resume the context that is now on the top of the execution context stack as the running execution context.
    // NOTE: We don't support this concept yet.

    // 24. If result.[[Type]] is not normal, throw a TypeError exception.
    if (result.type() != Completion::Type::Normal)
        return vm.throw_completion<TypeError>(global_object, ErrorType::ShadowRealmEvaluateAbruptCompletion);

    // 25. Return ? GetWrappedValue(callerRealm, result.[[Value]]).
    return get_wrapped_value(global_object, caller_realm, *result.value());

    // NOTE: Also see "Editor's Note" in the spec regarding the TypeError above.
}

// 3.1.2 ShadowRealmImportValue ( specifierString, exportNameString, callerRealm, evalRealm, evalContext ), https://tc39.es/proposal-shadowrealm/#sec-shadowrealmimportvalue
ThrowCompletionOr<Value> shadow_realm_import_value(GlobalObject& global_object, String specifier_string, String export_name_string, Realm& caller_realm, Realm& eval_realm, ExecutionContext& eval_context)
{
    auto& vm = global_object.vm();

    // 1. Assert: Type(specifierString) is String.
    // 2. Assert: Type(exportNameString) is String.
    // 3. Assert: callerRealm is a Realm Record.
    // 4. Assert: evalRealm is a Realm Record.
    // 5. Assert: evalContext is an execution context associated to a ShadowRealm instance's [[ExecutionContext]].

    // 6. Let innerCapability be ! NewPromiseCapability(%Promise%).
    auto inner_capability = MUST(new_promise_capability(global_object, global_object.promise_constructor()));

    // 7. Let runningContext be the running execution context.
    // 8. If runningContext is not already suspended, suspend runningContext.
    // NOTE: We don't support this concept yet.

    // 9. Push evalContext onto the execution context stack; evalContext is now the running execution context.
    TRY(vm.push_execution_context(eval_context, eval_realm.global_object()));

    // 10. Perform ! HostImportModuleDynamically(null, specifierString, innerCapability).
    vm.host_import_module_dynamically(Empty {}, ModuleRequest { move(specifier_string) }, inner_capability);

    // 11. Suspend evalContext and remove it from the execution context stack.
    // NOTE: We don't support this concept yet.
    vm.pop_execution_context();

    // 12. Resume the context that is now on the top of the execution context stack as the running execution context.
    // NOTE: We don't support this concept yet.

    // 13. Let steps be the steps of an ExportGetter function as described below.
    // 14. Let onFulfilled be ! CreateBuiltinFunction(steps, 1, "", « [[ExportNameString]] », callerRealm).
    // 15. Set onFulfilled.[[ExportNameString]] to exportNameString.
    // FIXME: Support passing a realm to NativeFunction::create()
    (void)caller_realm;
    auto* on_fulfilled = NativeFunction::create(
        global_object,
        "",
        [string = move(export_name_string)](auto& vm, auto& global_object) -> ThrowCompletionOr<Value> {
            // 1. Assert: exports is a module namespace exotic object.
            VERIFY(vm.argument(0).is_object());
            auto& exports = vm.argument(0).as_object();
            VERIFY(is<ModuleNamespaceObject>(exports));

            // 2. Let f be the active function object.
            auto* function = vm.running_execution_context().function;

            // 3. Let string be f.[[ExportNameString]].
            // 4. Assert: Type(string) is String.

            // 5. Let hasOwn be ? HasOwnProperty(exports, string).
            auto has_own = TRY(exports.has_own_property(string));

            // 6. If hasOwn is false, throw a TypeError exception.
            if (!has_own)
                return vm.template throw_completion<TypeError>(global_object, ErrorType::MissingRequiredProperty, string);

            // 7. Let value be ? Get(exports, string).
            auto value = TRY(exports.get(string));

            // 8. Let realm be f.[[Realm]].
            auto* realm = function->realm();
            VERIFY(realm);

            // 9. Return ? GetWrappedValue(realm, value).
            return get_wrapped_value(global_object, *realm, value);
        });
    on_fulfilled->define_direct_property(vm.names.length, Value(1), Attribute::Configurable);
    on_fulfilled->define_direct_property(vm.names.name, js_string(vm, String::empty()), Attribute::Configurable);

    // 16. Let promiseCapability be ! NewPromiseCapability(%Promise%).
    auto promise_capability = MUST(new_promise_capability(global_object, global_object.promise_constructor()));

    // NOTE: Even though the spec tells us to use %ThrowTypeError%, it's not observable if we actually do.
    // Throw a nicer TypeError forwarding the import error message instead (we know the argument is an Error object).
    auto* throw_type_error = NativeFunction::create(global_object, {}, [](auto& vm, auto& global_object) -> ThrowCompletionOr<Value> {
        return vm.template throw_completion<TypeError>(global_object, vm.argument(0).as_object().get_without_side_effects(vm.names.message).as_string().string());
    });

    // 17. Return ! PerformPromiseThen(innerCapability.[[Promise]], onFulfilled, callerRealm.[[Intrinsics]].[[%ThrowTypeError%]], promiseCapability).
    return verify_cast<Promise>(inner_capability.promise)->perform_then(on_fulfilled, throw_type_error, promise_capability);
}

// 3.1.3 GetWrappedValue ( callerRealm, value ), https://tc39.es/proposal-shadowrealm/#sec-getwrappedvalue
ThrowCompletionOr<Value> get_wrapped_value(GlobalObject& global_object, Realm& caller_realm, Value value)
{
    auto& vm = global_object.vm();

    // 1. Assert: callerRealm is a Realm Record.

    // 2. If Type(value) is Object, then
    if (value.is_object()) {
        // a. If IsCallable(value) is false, throw a TypeError exception.
        if (!value.is_function())
            return vm.throw_completion<TypeError>(global_object, ErrorType::ShadowRealmWrappedValueNonFunctionObject, value);

        // b. Return ! WrappedFunctionCreate(callerRealm, value).
        return WrappedFunction::create(global_object, caller_realm, value.as_function());
    }

    // 3. Return value.
    return value;
}

}
