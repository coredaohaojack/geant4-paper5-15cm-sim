"""
universal_worker.py - Parallel worker for Geant4 Simulation
Runs on GitHub Actions Swarm (13 active accounts x 10 runners).
Uploads 1-to-1 CSV results to 02_Flux_Data/<sample>/
Uploads ROOT files directly to 03_Simulation_Outputs/<sample>/ (No zip)
"""

import argparse
import os
import sys
import time
import json
import random
import subprocess
from pathlib import Path
import pandas as pd

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

import gdrive_helper
from googleapiclient.http import MediaFileUpload

def parse_args():
    parser = argparse.ArgumentParser(description="Geant4 Swarm Worker")
    parser.add_argument("--runner-id", type=int, required=True, help="Runner ID (1..10)")
    parser.add_argument("--num-runners", type=int, default=10, help="Total runners on this account")
    parser.add_argument("--primaries", type=int, default=1000000, help="Primaries per job")
    parser.add_argument("--gdrive-folder", type=str, required=True, help="Google Drive Project Folder ID")
    parser.add_argument("--plan-file", type=str, default="plan_swarm.json", help="Plan JSON file")
    parser.add_argument("--manifest-file", type=str, default="manifest.csv", help="Manifest CSV file")
    parser.add_argument("--github-user", type=str, default=None, help="GitHub user / account name")
    parser.add_argument("--sim-bin", type=str, default="./build/Sim", help="Path to Sim executable")
    return parser.parse_args()

def get_or_create_subfolder(service, parent_id, name):
    q = f"'{parent_id}' in parents and name = '{name}' and mimeType = 'application/vnd.google-apps.folder' and trashed = false"
    for attempt in range(5):
        try:
            res = service.files().list(q=q, fields="files(id, name)").execute().get("files", [])
            if res:
                return res[0]["id"]
            body = {"name": name, "mimeType": "application/vnd.google-apps.folder", "parents": [parent_id]}
            f = service.files().create(body=body, fields="id").execute()
            return f["id"]
        except Exception as e:
            time.sleep(1.0 + random.uniform(0.5, 2.0))
    raise RuntimeError(f"Failed to get_or_create_subfolder {name}")

def upload_file_to_drive(service, file_path: Path, parent_folder_id: str):
    q = f"'{parent_folder_id}' in parents and name = '{file_path.name}' and trashed = false"
    for attempt in range(5):
        try:
            res = service.files().list(q=q, fields="files(id)").execute().get("files", [])
            media = MediaFileUpload(str(file_path), resumable=True)
            if res:
                f_id = res[0]["id"]
                service.files().update(fileId=f_id, media_body=media).execute()
                return f_id
            body = {"name": file_path.name, "parents": [parent_folder_id]}
            new_f = service.files().create(body=body, media_body=media, fields="id").execute()
            return new_f["id"]
        except Exception as e:
            time.sleep(1.0 + random.uniform(0.5, 2.0))
    raise RuntimeError(f"Failed to upload {file_path.name}")

def main():
    args = parse_args()
    acc_name = args.github_user or os.environ.get("GITHUB_REPOSITORY_OWNER") or "runner"

    print("=" * 80)
    print(f"STARTING GEANT4 RUNNER #{args.runner_id} (Account: {acc_name})")
    print(f"Primaries per job: {args.primaries:,}")
    print(f"Plan file: {args.plan_file}")
    print("=" * 80)

    sim_bin = Path(args.sim_bin)
    if not sim_bin.exists():
        raise FileNotFoundError(f"Sim binary not found: {sim_bin}")

    # 1. Connect Google Drive
    print(">>> [DRIVE] Connecting to Google Drive...")
    service = gdrive_helper.get_drive_service()

    # Read pre-cached drive_folders.json if present
    cached_folders_file = Path("drive_folders.json")
    if cached_folders_file.exists():
        with open(cached_folders_file, "r", encoding="utf-8") as f:
            cf = json.load(f)
        flux_sub_map = cf.get("flux_subfolders", {})
        outputs_sub_map = cf.get("outputs_subfolders", {})
        print(f"Loaded {len(flux_sub_map)} pre-cached subfolders from drive_folders.json (0 API calls needed)!")
    else:
        subfolders = gdrive_helper.get_or_create_subfolders(service, parent_id=args.gdrive_folder)
        flux_id = subfolders.get("02_Flux_Data")
        outputs_id = subfolders.get("03_Simulation_Outputs")

        manifest_p = Path(args.manifest_file)
        if manifest_p.exists():
            df_m = pd.read_csv(manifest_p)
            samples = sorted(df_m["sample"].unique().tolist())
        else:
            samples = ["S0", "S1", "S2", "S3", "S4", "S7"]

        flux_sub_map = {}
        outputs_sub_map = {}
        for s in samples:
            flux_sub_map[s] = get_or_create_subfolder(service, flux_id, s)
            outputs_sub_map[s] = get_or_create_subfolder(service, outputs_id, s)

    # 2. Load Plan for this runner
    plan_path = Path(args.plan_file)
    if not plan_path.exists():
        raise FileNotFoundError(f"Plan file not found: {plan_path}")

    with open(plan_path, "r", encoding="utf-8") as f:
        full_plan = json.load(f)

    acc_plan = full_plan.get(acc_name, {})
    my_jobs = acc_plan.get(str(args.runner_id), [])

    print(f"\nAssigned {len(my_jobs)} jobs to runner #{args.runner_id} on account '{acc_name}':")
    for j in my_jobs:
        print(f"  • {j['job_name']}")

    if not my_jobs:
        print("No jobs assigned to this runner. Exiting cleanly.")
        return

    # 3. Process each job
    completed = 0
    total = len(my_jobs)

    for idx, job in enumerate(my_jobs, start=1):
        job_name = job["job_name"]
        sample = job["sample"]
        macro_name = job["macro_file"]
        csv_filename = job["csv_filename"]
        root_filename = job["root_filename"]

        dest_flux_folder = flux_sub_map[sample]
        dest_outputs_folder = outputs_sub_map[sample]

        # Check if already on Drive with retry
        is_on_drive = False
        for _ in range(3):
            try:
                q_check = f"'{dest_flux_folder}' in parents and name = '{csv_filename}' and trashed = false"
                res_chk = service.files().list(q=q_check, fields="files(id)").execute().get("files", [])
                if res_chk:
                    is_on_drive = True
                break
            except Exception:
                time.sleep(1)

        if is_on_drive:
            print(f"  [{idx}/{total}] SKIP: {job_name} already uploaded to Drive.")
            completed += 1
            continue

        print(f"\n[{idx}/{total}] Executing job: {job_name} (Macro: {macro_name})")
        macro_path = Path("macros") / macro_name
        if not macro_path.exists():
            raise FileNotFoundError(f"Macro not found: {macro_path}")

        # Clean previous local artifacts
        csv_process = Path("process_results.csv")
        if csv_process.exists():
            csv_process.unlink()

        t0 = time.time()
        res_run = subprocess.run([str(sim_bin.resolve()), macro_name], capture_output=True, text=True)
        t_run = time.time() - t0

        if res_run.returncode != 0:
            print(f"  [ERROR] Execution failed for {macro_name} (code {res_run.returncode})")
            print(res_run.stderr[-1000:])
            continue

        print(f"  • Sim run completed in {t_run:.2f} s")

        # Parse CSV
        if not csv_process.exists():
            print(f"  [ERROR] process_results.csv not found after running {macro_name}!")
            continue

        df_out = pd.read_csv(csv_process)
        if df_out.empty:
            print(f"  [ERROR] process_results.csv is empty!")
            continue

        last_row = df_out.iloc[-1:]
        local_job_csv = Path(csv_filename)
        last_row.to_csv(local_job_csv, index=False)

        # Upload CSV to 02_Flux_Data/<sample>/
        upload_file_to_drive(service, local_job_csv, dest_flux_folder)
        print(f"  • Uploaded CSV -> 02_Flux_Data/{sample}/{csv_filename}")
        local_job_csv.unlink()

        # Upload ROOT file to 03_Simulation_Outputs/<sample>/
        local_root = Path(root_filename)
        if local_root.exists():
            upload_file_to_drive(service, local_root, dest_outputs_folder)
            print(f"  • Uploaded ROOT -> 03_Simulation_Outputs/{sample}/{root_filename}")
            local_root.unlink()
        else:
            print(f"  [WARN] ROOT file {root_filename} not found locally.")

        completed += 1

    print("\n" + "=" * 80)
    print(f"RUNNER #{args.runner_id} COMPLETED: {completed}/{total} JOBS FINISHED!")
    print("=" * 80)

if __name__ == "__main__":
    main()
