import subprocess
import datetime
import os
import time

# --- 설정 ---

# ⭐️ quicsample 실행 파일의 전체 경로
QUICSAMPLE_PATH = '/home/woochan/widen/msquic_leo/build/bin/Release/quicsample'

# ⭐️ Linux는 'timeout', macOS(coreutils)는 'gtimeout'
# 사용 예: ['timeout', '60s'] 또는 ['gtimeout', '60s']
# 타임아웃을 사용하지 않으려면 빈 리스트로 두세요: []
TIMEOUT_COMMAND = ['timeout', '60s']

# 테스트할 포트 조합 (이 목록을 기반으로 9가지 조합 생성)
PORTS_FOR_PAIRS = [20001, 20002, 20003]

# 조합별 실행 횟수
RUNS_PER_PAIR = 5

# 첫 번째 클라이언트 실행 후 대기 시간 (초)
DELAY_BETWEEN_CLIENTS = 10

# 로그를 저장할 디렉토리 이름
LOG_DIRECTORY = 'client_concurrent_log'

# --- 스크립트 본문 ---

def run_concurrent_experiment():
    """quicsample 클라이언트를 10초 간격으로 동시에 실행하고 로그를 남깁니다."""

    if not os.path.exists(QUICSAMPLE_PATH):
        print(f"[치명적 오류] '{QUICSAMPLE_PATH}' 파일을 찾을 수 없습니다.")
        return
    
    # TIMEOUT_COMMAND가 비어있지 않다면, 해당 명령어가 존재하는지 확인
    if TIMEOUT_COMMAND and not shutil.which(TIMEOUT_COMMAND[0]):
        print(f"[치명적 오류] '{TIMEOUT_COMMAND[0]}' 명령어를 찾을 수 없습니다.")
        print("스크립트 상단의 TIMEOUT_COMMAND 변수를 확인하거나 빈 리스트( [] )로 변경하세요.")
        return

    if not os.path.exists(LOG_DIRECTORY):
        os.makedirs(LOG_DIRECTORY)
        print(f"'{LOG_DIRECTORY}' 디렉토리를 생성했습니다.")

    # 9가지 조합 (3*3) * 5회 = 45회
    total_runs = len(PORTS_FOR_PAIRS) * len(PORTS_FOR_PAIRS) * RUNS_PER_PAIR
    current_run = 0
    
    base_cmd_args = [
        '-client',
        '-target:54.67.60.13',
        '-unsecure',
        '-download:1000000000'
    ]

    print("="*50)
    print("자동 동시 클라이언트 테스트를 시작합니다.")
    print(f"테스트 조합: {len(PORTS_FOR_PAIRS) * len(PORTS_FOR_PAIRS)}개")
    print(f"조합당 반복: {RUNS_PER_PAIR}회")
    print(f"총 실행 쌍(Pair): {total_runs}")
    print("="*50)

    try:
        for run_num in range(1, RUNS_PER_PAIR + 1):
            print(f"\n--- 전체 {RUNS_PER_PAIR}회 중 {run_num}번째 실행 ---")
            
            for port1 in PORTS_FOR_PAIRS:
                for port2 in PORTS_FOR_PAIRS:
                    
                    current_run += 1
                    timestamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
                    
                    # 로그 파일 이름 (한 쌍의 실행임을 명확히 표시)
                    base_name = f"{timestamp}_run{run_num:02d}_{port1}s-then-{port2}s"
                    log1_filename = f"{base_name}_client1_p{port1}.log"
                    log2_filename = f"{base_name}_client2_p{port2}.log"
                    
                    log1_filepath = os.path.join(LOG_DIRECTORY, log1_filename)
                    log2_filepath = os.path.join(LOG_DIRECTORY, log2_filename)

                    # 실행할 명령어 조합
                    cmd1 = TIMEOUT_COMMAND + [QUICSAMPLE_PATH, f'-port:{port1}'] + base_cmd_args
                    cmd2 = TIMEOUT_COMMAND + [QUICSAMPLE_PATH, f'-port:{port2}'] + base_cmd_args

                    print(f"\n({current_run}/{total_runs}) Pair: {port1} -> (10s) -> {port2}")

                    # 로그 파일을 연다 (프로세스가 종료될 때까지 열려 있어야 함)
                    log1_file = open(log1_filepath, 'w')
                    log2_file = open(log2_filepath, 'w')
                    
                    proc1 = None
                    proc2 = None
                    
                    try:
                        # Client 1 시작
                        print(f"  -> Client 1 (Port {port1}) 시작. 로그: {log1_filename}")
                        proc1 = subprocess.Popen(
                            cmd1,
                            stdout=log1_file,
                            stderr=subprocess.STDOUT,
                            text=True
                        )
                        
                        # 10초 대기
                        print(f"  -> {DELAY_BETWEEN_CLIENTS}초 대기...")
                        time.sleep(DELAY_BETWEEN_CLIENTS)
                        
                        # Client 2 시작
                        print(f"  -> Client 2 (Port {port2}) 시작. 로그: {log2_filename}")
                        proc2 = subprocess.Popen(
                            cmd2,
                            stdout=log2_file,
                            stderr=subprocess.STDOUT,
                            text=True
                        )
                        
                        print("  -> 두 클라이언트 실행 중. 완료 대기...")
                        
                        # 두 프로세스가 모두 끝날 때까지 기다림
                        # .wait()는 프로세스가 끝날 때까지 스크립트를 차단(block)함
                        proc1_return = proc1.wait()
                        proc2_return = proc2.wait()
                        
                        print(f"  -> Pair 완료. (Client 1 종료 코드: {proc1_return}, Client 2 종료 코드: {proc2_return})")

                    except (Exception, KeyboardInterrupt) as e:
                        print(f"  [오류/중단] 테스트 쌍 실행 중 문제 발생: {e}")
                        # 오류 발생 시 실행 중인 프로세스 강제 종료
                        if proc1 and proc1.poll() is None:
                            proc1.terminate()
                            print("  -> Client 1 강제 종료")
                        if proc2 and proc2.poll() is None:
                            proc2.terminate()
                            print("  -> Client 2 강제 종료")
                    finally:
                        # 파일 핸들러를 닫음
                        log1_file.close()
                        log2_file.close()

    except KeyboardInterrupt:
        print("\n" + "="*50)
        print("사용자에 의해 스크립트가 중단되었습니다.")
        print("="*50)
    else:
        print("\n" + "="*50)
        print("모든 동시 테스트를 완료했습니다.")
        print(f"로그는 '{LOG_DIRECTORY}' 디렉토리에 저장되었습니다.")
        print("="*50)

# 이 코드를 실행하기 전에 shutil을 import 해야 합니다.
import shutil

if __name__ == '__main__':
    run_concurrent_experiment()