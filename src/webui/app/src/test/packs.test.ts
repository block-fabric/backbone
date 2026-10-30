// What a sidechain's pages use of its daemon beyond the kit's (doc/unified-app.md §13.5 #59): read from its local.cpp at the
// pinned commit by scripts/packs.mjs, written into the packs' index, named by the app in that daemon's -rpcwhitelist.
import { describe, expect, it } from 'vitest';
// @ts-expect-error: a build script of plain JavaScript, without types.
import { chainMethods, withShim } from '../../scripts/packs.mjs';

const LOCAL = `
std::map<std::string, Bridge::Thread> RpcMethods()
{
    return {
        // What the node says: read only. A brace in a comment: {"notamethod", WORKER}
        {"getbitassetsinfo", Bridge::Thread::WORKER},
        {"listmyassets", WORKER},
        {"swapasset", Bridge::Thread::LONG},
    };
}

std::map<std::string, confirm::Method> ConfirmedRpcMethods()
{
    return {
        {"swapasset", {confirm::Method::Kind::SPEND, Bridge::Thread::LONG, confirm::Generic("Swap {a} for {b}", "Swap", "x")}},
        {"sendasset", {confirm::Method::Kind::SPEND, Bridge::Thread::LONG, confirm::Generic("Send", "Send", "y")}},
    };
}

void RegisterHost(Bridge& bridge, NodeFn node, guard::ConfirmFn confirm, confirm::KeysFn keys)
{
    bridge.RegisterHost("bitassets.logo", [node](const UniValue& params) { return Logo(params, "}"); });
    for (const auto& [method, file] : {std::pair{"hivemind.store.seals", "seals.json"}}) bridge.RegisterHost(method, nullptr);
    bridge.RegisterHost("unknown.thing", nullptr);
}
`;

describe('the methods a pack uses of its daemon', () => {
  it('reads its reads, its confirmed methods (a confirmed one never a read) and its host methods, past strings and comments', () => {
    const m = chainMethods(LOCAL, ['bitassets', 'hivemind', 'layers']);
    expect(m.reads).toEqual(['getbitassetsinfo', 'listmyassets']);
    expect(m.confirmed).toEqual(['sendasset', 'swapasset']);
    // Only names of a sidechain's own ("<stem>.<what>"): not a file's name, not another prefix.
    expect(m.host).toEqual(['bitassets.logo', 'hivemind.store.seals']);
  });
  it('gives nothing for a file without them', () => {
    expect(chainMethods('int main() { return 0; }', ['layers'])).toEqual({ reads: [], confirmed: [], host: [] });
  });
  it("puts the shim before the pack's own script", () => {
    expect(withShim('<head><meta charset="utf-8"><script type="module" src="x.js"></script></head>')).toContain('<script src="/packs/shim.js"></script>\n    <script type="module"');
  });
});
