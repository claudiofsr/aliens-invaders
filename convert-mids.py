#!/usr/bin/env python3
"""
Script para conversão em lote de arquivos MIDI para arquivos C++ (.cc)
utilizando a ferramenta midi2cpp.
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class ConversionJob:
    """Representa uma tarefa imutável de conversão."""
    input_file: Path
    track_id: str
    output_file: Path


# ==============================================================================
# Funções Puras (Pure Functions)
# ==============================================================================

def extract_track_id(filename: str) -> str | None:
    """Extrai os dois dígitos iniciais do nome do arquivo (ex: '01' de '01-Mozat...')."""
    match = re.match(r"^(\d{2})-", filename)
    return match.group(1) if match else None


def is_midi_file(path: Path) -> bool:
    """Verifica se o arquivo possui extensão MIDI suportada (.mid ou .midi)."""
    return path.suffix.lower() in {".mid", ".midi"}


def build_job(file_path: Path, output_dir: Path) -> ConversionJob | None:
    """Cria um ConversionJob a partir de um arquivo, se o formato for válido."""
    if not is_midi_file(file_path):
        return None

    track_id = extract_track_id(file_path.name)
    if not track_id:
        return None

    output_filename = f"music_{track_id}.cc"
    return ConversionJob(
        input_file=file_path,
        track_id=track_id,
        output_file=output_dir / output_filename,
    )


def build_command_args(job: ConversionJob) -> list[str]:
    """
    Constrói a lista de argumentos para o subprocesso.
    Evita problemas de quoting com caminhos contendo espaços e parênteses.
    """
    return [
        "midi2cpp",
        str(job.input_file),
        "-t", job.track_id,
        "-o", str(job.output_file),
        "-s",
        "-l", "0.05",
        # "--orchestrate",
        "-v",
    ]


# ==============================================================================
# Classe Controladora (Orquestração e Efeitos Colaterais)
# ==============================================================================

class MidiBatchConverter:
    """Orquestra a descoberta, validação e execução das conversões."""

    def __init__(self, source_dir: Path, target_dir: Path, dry_run: bool = False):
        self.source_dir = source_dir
        self.target_dir = target_dir
        self.dry_run = dry_run

    def validate_environment(self) -> None:
        """Valida a existência dos diretórios e da ferramenta midi2cpp."""
        if not self.source_dir.is_dir():
            raise FileNotFoundError(f"Diretório de entrada não encontrado: {self.source_dir}")

        if not self.dry_run and shutil.which("midi2cpp") is None:
            raise EnvironmentError(
                "O binário 'midi2cpp' não foi encontrado no PATH do sistema."
            )

    def discover_jobs(self) -> list[ConversionJob]:
        """Varre o diretório e mapeia os jobs de conversão ordenados."""
        jobs: list[ConversionJob] = []

        for item in self.source_dir.iterdir():
            if item.is_file():
                job = build_job(item, self.target_dir)
                if job:
                    jobs.append(job)

        # Ordena crescentemente pelo identificador da track ('01', '02', ...)
        return sorted(jobs, key=lambda j: j.track_id)

    def execute_job(self, job: ConversionJob) -> bool:
        """Executa um job individualmente."""
        cmd = build_command_args(job)
        cmd_str = " ".join(f'"{arg}"' if " " in arg else arg for arg in cmd)

        print(f"\n[JOB {job.track_id}] Convertendo: {job.input_file.name}")
        print(f"  -> Comando: {cmd_str}")

        if self.dry_run:
            print("  -> [DRY-RUN] Nenhuma alteração feita.")
            return True

        try:
            result = subprocess.run(cmd, check=True, text=True)
            return result.returncode == 0
        except subprocess.CalledProcessError as exc:
            print(f"  -> [ERRO] Falha ao processar {job.input_file.name}: {exc}", file=sys.stderr)
            return False

    def run(self) -> int:
        """Executa o pipeline completo de conversão."""
        self.validate_environment()

        jobs = self.discover_jobs()
        if not jobs:
            print(f"Nenhum arquivo MIDI correspondente encontrado em '{self.source_dir}'.")
            return 0

        if not self.dry_run:
            self.target_dir.mkdir(parents=True, exist_ok=True)

        print(f"Iniciando conversão de {len(jobs)} arquivo(s)...")

        success_count = 0
        for job in jobs:
            if self.execute_job(job):
                success_count += 1

        print(f"\nConcluído: {success_count}/{len(jobs)} arquivos processados com sucesso.")
        return 0 if success_count == len(jobs) else 1


# ==============================================================================
# Ponto de Entrada (CLI)
# ==============================================================================

def parse_arguments() -> argparse.Namespace:
    epilog_examples = """exemplos de uso:
  %(prog)s
  %(prog)s -d
  %(prog)s --dry-run
  %(prog)s -s mids -t src/musics
  %(prog)s --source mids --target src/musics
  %(prog)s -s ./outros_mids -t ./build/sons --dry-run
"""

    parser = argparse.ArgumentParser(
        description="Converte arquivos MIDI da pasta mids/ para C++ (.cc) em src/musics/.",
        epilog=epilog_examples,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )

    parser.add_argument(
        "-s", "--source",
        type=Path,
        default=Path("mids"),
        help="Diretório contendo os arquivos .mid/.midi (padrão: mids)",
    )
    parser.add_argument(
        "-t", "--target",
        type=Path,
        default=Path("src/musics"),
        help="Diretório de saída para os arquivos .cc (padrão: src/musics)",
    )
    parser.add_argument(
        "-d", "--dry-run",
        action="store_true",
        help="Apenas exibe os comandos que seriam executados, sem executá-los.",
    )

    return parser.parse_args()


def main() -> None:
    args = parse_arguments()
    converter = MidiBatchConverter(
        source_dir=args.source,
        target_dir=args.target,
        dry_run=args.dry_run,
    )

    try:
        sys.exit(converter.run())
    except (FileNotFoundError, EnvironmentError) as err:
        print(f"Erro: {err}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
