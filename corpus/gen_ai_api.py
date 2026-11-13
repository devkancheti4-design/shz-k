"""Generate AI-API transcripts with local models (no download), in the formats real AI services write:
  embeddings  -- one OpenAI-style embeddings response per line: {"object":"list","data":[{"object":"embedding",
                 "index":0,"embedding":[...896 float32 values...]}],"model":...,"usage":{...}}; the vectors are mean-pooled,
                 L2-normalised last hidden states of Qwen2.5-0.5B (MLX), float32, written by Python's json (each value
                 the shortest decimal of the float64 it widens to -- what any Python service writes)
  logprobs    -- one OpenAI-style chat.completion per line with "logprobs": {"content": [{"token", "logprob", "bytes",
                 "top_logprobs": [5 x {"token", "logprob", "bytes"}]}]}; Llama-3.2-1B-Instruct (MLX) writes the
                 completion (temperature 0.7, seeded); "bytes" are each token's TRUE bytes from the byte-level vocabulary
                 (so a token that is part of a UTF-8 character carries bytes its text cannot show, as in real APIs)
Inputs: texts of public datasets already in the local Hugging Face cache (ag_news, imdb, emotion, trec).
    python3 corpus/gen_ai_api.py embeddings|logprobs OUT.jsonl TARGET_MB"""
import glob, json, os, random, sys, time
import mlx.core as mx
from mlx_lm import load
import pyarrow.parquet as pq

def texts():
    out = []
    for d in sorted(glob.glob(os.path.expanduser('~/.cache/huggingface/hub/datasets--*'))):
        for f in sorted(glob.glob(d + '/snapshots/*/**/*.parquet', recursive=True))[:1]:
            t = pq.read_table(f)
            col = 'text' if 'text' in t.column_names else t.column_names[0]
            if col == 'text': out += [s for s in t.column(col).to_pylist() if isinstance(s, str) and 20 < len(s) < 2000]
    random.Random(20261113).shuffle(out)
    return out

def bytes_to_unicode():
    bs = list(range(ord('!'), ord('~') + 1)) + list(range(ord('¡'), ord('¬') + 1)) + list(range(ord('®'), ord('ÿ') + 1))
    cs = bs[:]; n = 0
    for b in range(256):
        if b not in bs: bs.append(b); cs.append(256 + n); n += 1
    return {chr(c): b for b, c in zip(bs, cs)}

def gen_embeddings(out, target):
    model, tok = load(os.path.expanduser('~/.cache/huggingface/hub/models--mlx-community--Qwen2.5-0.5B-Instruct-4bit/snapshots/') +
                      os.listdir(os.path.expanduser('~/.cache/huggingface/hub/models--mlx-community--Qwen2.5-0.5B-Instruct-4bit/snapshots/'))[0])
    n = 0; size = 0
    with open(out, 'w') as f:
        for i, t in enumerate(texts()):
            ids = tok.encode(t)[:256]
            h = model.model(mx.array(ids)[None])[0]                     # [T, D] last hidden states (after the final norm)
            v = mx.mean(h.astype(mx.float32), axis=0); v = v / mx.sqrt(mx.sum(v * v))
            emb = v.astype(mx.float32).tolist()
            line = json.dumps({"object": "list", "data": [{"object": "embedding", "index": 0, "embedding": emb}],
                               "model": "qwen2.5-0.5b-instruct", "usage": {"prompt_tokens": len(ids), "total_tokens": len(ids)}})
            f.write(line + '\n'); size += len(line) + 1; n += 1
            if size >= target: break
    print('embeddings: %d responses, %d bytes' % (n, size))

def gen_logprobs(out, target):
    root = os.path.expanduser('~/.cache/huggingface/hub/models--mlx-community--Llama-3.2-1B-Instruct-4bit/snapshots/')
    model, tok = load(root + os.listdir(root)[0])
    b2u = bytes_to_unicode()
    vocab_tokens = tok._tokenizer.convert_ids_to_tokens if hasattr(tok, '_tokenizer') else tok.convert_ids_to_tokens
    def true_bytes(i):
        s = vocab_tokens(i)
        try: return [b2u[ch] for ch in s]
        except KeyError: return list(tok.decode([i]).encode('utf-8'))
    rng = random.Random(20261113); mx.random.seed(20261113)
    n = 0; size = 0; t0 = time.time()
    with open(out, 'w') as f:
        for k, t in enumerate(texts()):
            prompt = 'Write a short reply to this message:\n\n' + t[:600]
            ids = tok.encode(tok.apply_chat_template([{"role": "user", "content": prompt}], add_generation_prompt=True, tokenize=False), add_special_tokens=False)
            from mlx_lm.models.cache import make_prompt_cache
            cache = make_prompt_cache(model)
            logits = model(mx.array(ids)[None], cache=cache)[0, -1]
            content, toks = [], []
            for step in range(96):
                lp = logits.astype(mx.float32) - mx.logsumexp(logits.astype(mx.float32))
                top = mx.argsort(-lp)[:5]
                nxt = mx.random.categorical(logits.astype(mx.float32) / 0.7).item()
                tops = [(int(j), float(lp[j].item())) for j in top.tolist()]
                content.append({"token": tok.decode([nxt]), "logprob": float(lp[nxt].item()), "bytes": true_bytes(nxt),
                                "top_logprobs": [{"token": tok.decode([j]), "logprob": l, "bytes": true_bytes(j)} for j, l in tops]})
                toks.append(nxt)
                if nxt in (tok.eos_token_id, 128009): break
                logits = model(mx.array([[nxt]]), cache=cache)[0, -1]
            rec = {"id": "chatcmpl-%010d" % k, "object": "chat.completion", "created": 1762992000 + 7 * k, "model": "llama-3.2-1b-instruct",
                   "choices": [{"index": 0, "message": {"role": "assistant", "content": tok.decode(toks)}, "logprobs": {"content": content},
                                "finish_reason": "stop" if len(toks) < 96 else "length"}],
                   "usage": {"prompt_tokens": len(ids), "completion_tokens": len(toks), "total_tokens": len(ids) + len(toks)}}
            line = json.dumps(rec, ensure_ascii=False)
            f.write(line + '\n'); size += len(line.encode()) + 1; n += 1
            if size >= target: break
    print('logprobs: %d completions, %d bytes, %.0fs' % (n, size, time.time() - t0))

if __name__ == '__main__':
    kind, out, mb = sys.argv[1], sys.argv[2], float(sys.argv[3])
    (gen_embeddings if kind == 'embeddings' else gen_logprobs)(out, int(mb * 1e6))
