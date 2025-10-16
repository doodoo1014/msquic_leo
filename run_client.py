import subprocess
import datetime
import os

# --- 설정 ---

# ⭐️ quicsample 실행 파일의 전체 경로를 지정하세요.
QUICSAMPLE_PATH = '/home/woochan/widen/msquic_leo/build/bin/Release/quicsample'

# 테스트할 포트 목록
PORTS_TO_TEST = [20001, 20002, 20003, 20004]

# 포트별 실행 횟수
RUNS_PER_PORT = 2

# 로그를 저장할 디렉토리 이름 (스크립트를 실행하는 위치에 생성됩니다)
LOG_DIRECTORY = 'client_log'

# --- 스크립트 본문 ---

def run_experiment():
    """quicsample 클라이언트를 설정에 따라 반복 실행하고 로그를 남깁니다."""

    if not os.path.exists(QUICSAMPLE_PATH):
        print(f"[치명적 오류] '{QUICSAMPLE_PATH}' 파일을 찾을 수 없습니다.")
        print("스크립트 상단의 QUICSAMPLE_PATH 변수가 올바른지 확인하세요.")
        return

    if not os.path.exists(LOG_DIRECTORY):
        os.makedirs(LOG_DIRECTORY)
        print(f"'{LOG_DIRECTORY}' 디렉토리를 생성했습니다.")

    total_runs = len(PORTS_TO_TEST) * RUNS_PER_PORT
    current_run = 0

    print("="*40)
    print("자동 클라이언트 테스트를 시작합니다.")
    print(f"총 실행 횟수: {total_runs}")
    print("="*40)

    for port in PORTS_TO_TEST:
        print(f"\n--- 포트 {port} 테스트 시작 ({RUNS_PER_PORT}회) ---")
        
        for i in range(1, RUNS_PER_PORT + 1):
            current_run += 1
            
            timestamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
            log_filename = f"{timestamp}_port{port}_run{i:02d}.log"
            log_filepath = os.path.join(LOG_DIRECTORY, log_filename)
            
            command = [
                QUICSAMPLE_PATH,
                '-client',
                '-target:127.0.0.1',
                f'-port:{port}',
                '-unsecure',
                '-download:1000000000'
            ]
            
            print(f"({current_run}/{total_runs}) 실행 중... -> {log_filename}")
            
            try:
                with open(log_filepath, 'w') as log_file:
                    subprocess.run(
                        command,
                        stdout=log_file,
                        stderr=subprocess.STDOUT,
                        check=True,
                        text=True
                    )
            except subprocess.CalledProcessError as e:
                print(f"  [오류] {log_filename} 실행 중 오류 발생 (종료 코드: {e.returncode})")
            
    print("\n" + "="*40)
    print("모든 테스트를 완료했습니다.")
    print(f"로그는 '{LOG_DIRECTORY}' 디렉토리에 저장되었습니다.")
    print("="*40)


if __name__ == '__main__':
    run_experiment()