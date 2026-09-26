#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Aliens Invaders — Timeline Performance Profiler (perf_monitor.py)

Captures live per-second 'perf top' snapshots while aliens-invaders is running,
logging them with high-resolution timestamps to an output file for state analysis
(Menu -> Start -> Combat -> Pause -> Quit).
"""

from __future__ import annotations

import argparse
from datetime import datetime
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time

DEFAULT_OUTPUT = "aliens-perf-timeline.log"
PROCESS_NAME = "aliens-invaders"


def find_target_pid() -> int | None:
    """Find PID of the running game binary."""
    try:
        out = subprocess.check_output(["pgrep", "-x", PROCESS_NAME], text=True)
        pids = [int(p.strip()) for p in out.strip().splitlines() if p.strip()]
        return pids[0] if pids else None
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None


def pid_alive(pid: int) -> bool:
    """Zero-cost liveness probe: signal 0 checks existence without
    actually sending a signal. 'perf top -p <pid>' does not reliably
    notice or exit when its target dies, so the capture loop must
    check this itself instead of trusting perf's own output."""
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        # Exists, just not signalable by us: still alive.
        return True


def wait_for_process(timeout_sec: float = 60.0) -> int | None:
    """Wait until aliens-invaders starts running."""
    print(f"[-] Aguardando o processo '{PROCESS_NAME}' iniciar...")
    start_time = time.time()
    while time.time() - start_time < timeout_sec:
        pid = find_target_pid()
        if pid:
            return pid
        time.sleep(0.5)
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description="Live per-second perf profiler timeline.")
    parser.add_argument(
        "-o", "--output",
        default=DEFAULT_OUTPUT,
        help=f"Output log file (default: {DEFAULT_OUTPUT})"
    )
    parser.add_argument(
        "-d", "--delay",
        type=int,
        default=1,
        help="Sampling interval in seconds (default: 1)"
    )
    args = parser.parse_args()

    output_path = Path(args.output).resolve()

    # 1. Localizar ou aguardar o processo do jogo
    pid = find_target_pid()
    if not pid:
        pid = wait_for_process()
        if not pid:
            print(f"[ERRO] O processo '{PROCESS_NAME}' não foi iniciado a tempo.", file=sys.stderr)
            return 1

    print(f"[+] Processo detectado! PID: {pid}")
    print(f"[+] Gravando linha do tempo em: {output_path.name} (amostra a cada {args.delay}s)")
    print("[+] Pressione Ctrl+C a qualquer momento ou saia do jogo (Q) para finalizar.\n")

    # Comando: perf top em modo texto puro com intervalo configurável
    perf_cmd = [
        "perf", "top",
        "--stdio",
        "-d", str(args.delay),
        "-p", str(pid)
    ]

    try:
        perf_proc = subprocess.Popen(
            perf_cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1
        )
    except FileNotFoundError:
        print("[ERRO] Comando 'perf' não encontrado no sistema.", file=sys.stderr)
        print("Instale o pacote 'perf' do seu kernel: sudo pacman -S linux-tools", file=sys.stderr)
        return 1
    except PermissionError:
        print("[ERRO] Permissão negada para executar o 'perf'.", file=sys.stderr)
        print("Execute com sudo ou ajuste /proc/sys/kernel/perf_event_paranoid", file=sys.stderr)
        return 1

    session_start = time.time()
    reading_count = 0

    with open(output_path, "w", encoding="utf-8") as out_file:
        out_file.write("=" * 80 + "\n")
        out_file.write(f" ALIENS INVADERS — PERF TIMELINE MONITOR\n")
        out_file.write(f" Target PID: {pid} | Interval: {args.delay}s | Date: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
        out_file.write("=" * 80 + "\n\n")
        out_file.flush()

        def handle_shutdown(signum, frame):
            print("\n[-] Interrupção solicitada. Finalizando captura...")
            if perf_proc.poll() is None:
                perf_proc.terminate()

        signal.signal(signal.SIGINT, handle_shutdown)
        signal.signal(signal.SIGTERM, handle_shutdown)

        current_block = []
        ansi_escape = re.compile(r'\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])')

        while perf_proc.poll() is None:
            line = perf_proc.stdout.readline()
            if not line:
                break

            clean_line = ansi_escape.sub('', line)

            # O cabeçalho 'Samples:' do perf top marca o início de uma nova leitura a cada 1s
            if clean_line.strip().startswith("PerfTop:") or clean_line.strip().startswith("Samples:"):
                if not pid_alive(pid):
                    # perf top keeps redrawing its last cached screen after the
                    # target exits instead of shutting down on its own; stop here
                    # rather than logging an unbounded run of stale duplicate reads.
                    stale_notice = (
                        f"\n{'=' * 80}\n"
                        f" [AVISO] PID {pid} nao esta mais em execucao; 'perf top' "
                        f"parou de amostrar mas continuou redesenhando a ultima tela "
                        f"em cache. Encerrando a captura automaticamente.\n"
                        f"{'=' * 80}\n"
                    )
                    out_file.write(stale_notice)
                    out_file.flush()
                    print("  [!] Processo do jogo encerrado; parando a captura automaticamente.")
                    if perf_proc.poll() is None:
                        perf_proc.terminate()
                    break

                reading_count += 1
                elapsed = time.time() - session_start
                mins, secs = divmod(int(elapsed), 60)
                now_str = datetime.now().strftime("%H:%M:%S")

                header = (
                    f"\n{'=' * 80}\n"
                    f" [{mins:02d}:{secs:02d}s | {now_str}] LEITURA #{reading_count} — PID {pid}\n"
                    f"{'=' * 80}\n"
                )

                out_file.write(header)
                out_file.write(clean_line)
                out_file.flush()

                # Feedback simplificado no terminal
                print(f"  • [{mins:02d}:{secs:02d}s] Leitura #{reading_count} capturada...")
            else:
                out_file.write(clean_line)
                out_file.flush()

        # Finalização da sessão
        elapsed = time.time() - session_start
        mins, secs = divmod(int(elapsed), 60)
        footer = (
            f"\n{'=' * 80}\n"
            f" FIM DA SESSÃO: Processo encerrado após {mins:02d}m{secs:02d}s ({reading_count} leituras).\n"
            f"{'=' * 80}\n"
        )
        out_file.write(footer)
        out_file.flush()

    print(f"\n[OK] Monitoramento concluído com sucesso!")
    print(f"[OK] Total de leituras: {reading_count} em {mins:02d}m{secs:02d}s")
    print(f"[OK] Arquivo gerado: {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
