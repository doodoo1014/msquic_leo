import subprocess
import datetime
import os
import argparse  # 👈 명령줄 인자 처리를 위해 추가

# --- 설정 ---

# ⭐️ quicsample 실행 파일의 전체 경로를 지정하세요.
QUICSAMPLE_PATH = '/home/woochan/widen/msquic_leo/build/bin/Release/quicsample'

# 기본 테스트 포트 목록 (파라미터가 없을 시 사용)
DEFAULT_PORTS_TO_TEST = [20001, 20002, 20003]

# 기본 실행 횟수 (파라미터가 없을 시 사용)
DEFAULT_RUNS_PER_PORT = 5

# 로그를 저장할 디렉토리 이름 (스크립트를 실행하는 위치에 생성됩니다)
LOG_DIRECTORY = 'client_log'

# --- 스크립트 본문 ---

def run_experiment(ports_to_run, num_runs):
    """quicsample 클라이언트를 설정에 따라 반복 실행하고 로그를 남깁니다.

    Args:
        ports_to_run (list): 테스트를 실행할 포트 번호의 리스트.
        num_runs (int): 포트당 실행할 횟수.
    """

    if not os.path.exists(QUICSAMPLE_PATH):
        print(f"[치명적 오류] '{QUICSAMPLE_PATH}' 파일을 찾을 수 없습니다.")
        print("스크립트 상단의 QUICSAMPLE_PATH 변수가 올바른지 확인하세요.")
        return

    if not os.path.exists(LOG_DIRECTORY):
        os.makedirs(LOG_DIRECTORY)
        print(f"'{LOG_DIRECTORY}' 디렉토리를 생성했습니다.")

    total_runs = len(ports_to_run) * num_runs
    current_run = 0

    print("="*40)
    print("자동 클라이언트 테스트를 시작합니다.")
    print(f"테스트 포트: {ports_to_run}")
    print(f"포트당 실행 횟수: {num_runs}")
    print(f"총 실행 횟수: {total_runs}")
    print("="*40)

    for port in ports_to_run:
        print(f"\n--- 포트 {port} 테스트 시작 ({num_runs}회) ---")
        
        for i in range(1, num_runs + 1):
            current_run += 1
            
            timestamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
            log_filename = f"{timestamp}_port{port}_run{i:02d}.log"
            log_filepath = os.path.join(LOG_DIRECTORY, log_filename)
            
            command = [
                QUICSAMPLE_PATH,
                '-client',
                '-target:54.67.60.13',
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
            except subprocess.TimeoutExpired:
                print(f"  [오류] {log_filename} 실행 중 타임아웃(60초) 발생")
            
    print("\n" + "="*40)
    print("모든 테스트를 완료했습니다.")
    print(f"로그는 '{LOG_DIRECTORY}' 디렉토리에 저장되었습니다.")
    print("="*40)


def main():
    """명령줄 인자를 파싱하고 실험을 실행합니다."""
    
    # 1. ArgumentParser 객체 생성
    parser = argparse.ArgumentParser(
        description='quicsample 클라이언트 자동 실행 스크립트',
        formatter_class=argparse.RawTextHelpFormatter  # 👈 help 메시지 줄바꿈 유지를 위해 추가
    )
    
    # 2. 인자 추가
    parser.add_argument(
        '-port', '--port',
        type=int,
        default=None,  # 👈 기본값을 None으로 하여, 인자가 주어졌는지 확인
        help=f'테스트할 특정 포트 번호.\n(지정하지 않으면 {DEFAULT_PORTS_TO_TEST} 모두 실행)'
    )
    
    parser.add_argument(
        '-run', '--run',
        type=int,
        default=DEFAULT_RUNS_PER_PORT,  # 👈 기본값을 상수로 지정
        help=f'포트당 실행할 횟수 (기본값: {DEFAULT_RUNS_PER_PORT})'
    )
    
    # 3. 인자 파싱
    args = parser.parse_args()
    
    # 4. 파싱된 인자에 따라 변수 설정
    ports_to_run = []
    
    if args.port is not None:
        # -port 인자가 주어졌으면 (예: -port=20001)
        ports_to_run = [args.port]
    else:
        # -port 인자가 없으면 기본 포트 목록 사용
        ports_to_run = DEFAULT_PORTS_TO_TEST
        
    # -run 인자는 값이 주어지지 않으면 default 값이 자동으로 사용됨
    num_runs = args.run
    
    # 5. 설정된 값으로 실험 함수 호출
    run_experiment(ports_to_run, num_runs)


if __name__ == '__main__':
    main()  # 👈 main 함수를 호출하도록 변경