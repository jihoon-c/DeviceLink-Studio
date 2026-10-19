"""On-demand loopback model server; only servers started here are stopped here."""
from __future__ import annotations

from contextlib import contextmanager
import json
import os
from pathlib import Path
import subprocess
import time
from urllib.request import ProxyHandler, build_opener

if __package__:
    from .demo_processes import ProcessJob
else:
    from demo_processes import ProcessJob


def model_names() -> list[str]:
    with build_opener(ProxyHandler({})).open('http://127.0.0.1:11434/api/tags', timeout=2) as response:
        return [model['name'] for model in json.load(response)['models']]


@contextmanager
def local_model(root: Path, model: str):
    process = None
    job = None
    try:
        try:
            names = model_names()
        except (OSError, ValueError, KeyError):
            executables = sorted((root / 'artifacts/local-llm').glob('ollama-v*/ollama.exe'), reverse=True)
            if not executables:
                raise RuntimeError('local_runtime_missing')
            job = ProcessJob()
            environment = dict(os.environ, OLLAMA_HOST='127.0.0.1:11434',
                               OLLAMA_MODELS=str(root / 'artifacts/local-llm/models'),
                               OLLAMA_NO_CLOUD='1', OLLAMA_CONTEXT_LENGTH='8192',
                               OLLAMA_MAX_LOADED_MODELS='1', OLLAMA_NUM_PARALLEL='1')
            environment.pop('OPENAI_API_KEY', None)
            process = subprocess.Popen([str(executables[0]), 'serve'], cwd=root, env=environment,
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                       creationflags=subprocess.CREATE_NO_WINDOW)
            job.attach(process.pid)
            deadline = time.monotonic() + 30
            while True:
                if process.poll() is not None:
                    raise RuntimeError('local_runtime_start_failed')
                try:
                    names = model_names()
                    break
                except (OSError, ValueError, KeyError):
                    if time.monotonic() >= deadline:
                        raise RuntimeError('local_runtime_start_timeout')
                    time.sleep(0.15)
        if model not in names:
            raise RuntimeError('local_model_missing')
        yield
    finally:
        if job is not None:
            job.close()
        if process is not None:
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.terminate()
                process.wait(timeout=3)
