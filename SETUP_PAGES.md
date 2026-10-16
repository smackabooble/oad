# Publish steps (do once)

1. **Game data host** (needs CORS + Range). Easiest: Hugging Face dataset.
   - Make a free account, create a *dataset* repo (e.g. `you/0ad-data`).
   - `pip install huggingface_hub && huggingface-cli login`
   - From your official 0 A.D. `data` folder: `huggingface-cli upload you/0ad-data ./data . --repo-type dataset`
   - Base URL: `https://huggingface.co/datasets/you/0ad-data/resolve/main/`
   - Keep each mod zip under 1.9 GB (`web/mkzip.py` splits them).
2. **Config:** `python3 tools/make_data_config.py ./data <BASE URL> docs/data-config.json`, commit, push.
3. **Repo public**, then Settings → Pages → Branch `main`, folder `/docs`.
4. Open `https://<user>.github.io/oad/` in Chrome/Edge 137+, click "Download and play".

Reset stored data: add `?reset` to the URL.
