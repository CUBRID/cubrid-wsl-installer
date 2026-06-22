# CUBRID 엔진 업그레이드 (Upgrade CUBRID Engine)

## [Korean]

본 문서는 **WSL2에 설치된 CUBRID서버를 새 버전으로 업그레이드하는 절차**를 설명합니다. WSL 환경의 CUBRID는 일반 Linux 환경의 CUBRID와 동일한 방식으로 업그레이드할 수 있습니다.

### **주의사항**

* 업그레이드 전에 반드시 `cubrid service stop` 으로 CUBRID 서비스를 정지합니다.
* 설정 파일(`cubrid.conf`, `cubrid_broker.conf`, `cm.conf` 등) 과 DB 위치 정보 파일(`databases.txt`등) 은 업그레이드 전에 반드시 백업해 둡니다.
* 서로 다른 **메이저 또는 마이너 버전** 사이에서는 DB 볼륨이 호환되지 않을 수 있으므로 `unloaddb` / `loaddb` 를 통한 마이그레이션이 필요합니다.
* 업그레이드 작업은 모두 WSL 내부의 `cubrid` 계정에서 수행해야 합니다. PowerShell 에서 다음 명령으로 접속할 수 있습니다.
```command
wsl -d CUBRID_For_WSL -u cubrid
```

---

### 업그레이드 시나리오 요약

| 시나리오 | 예시 | 권장 방법 |
| :--- | :--- | :--- |
| **패치 버전만 변경** | x.x.0 → x.x.1 | 설정 파일 백업 후 새 패키지로 덮어쓰기 |
| **메이저 / 마이너 버전 변경** | x.3 → x.4, x.0 → y.0 | `unloaddb` / `loaddb` 로 데이터 마이그레이션 |

> **TIP**: 현재 설치된 CUBRID 의 정확한 버전은 WSL 안에서 `cubrid_rel` 명령으로 확인할 수 있습니다.

---

### 1. 패치 버전 업그레이드 (동일 메이저 버전)

동일한 메이저 버전 안에서의 업그레이드(예: x.x.0 → x.x.1)는 DB 볼륨 형식이 호환되므로 **설정 파일만 보존**하면 됩니다.

#### 1-1. 서비스 정지 및 설정 파일 백업

WSL 안에서 다음 명령을 차례로 실행합니다.

```command
cubrid service stop

mkdir -p ~/cubrid_backup
rm -rf ~/cubrid_backup/conf
cp -a $CUBRID/conf                  ~/cubrid_backup/
cp    $CUBRID_DATABASES/databases.txt ~/cubrid_backup/
```

> **참고**: `conf` 디렉터리는 통째로 백업/복원해야 합니다.
> 사용자 정의 토큰 파일(`tde_keys`), 로케일/타임존 데이터 등 `cubrid.conf` 이외의 부수 파일까지 함께 보존됩니다. `cp -a` 는 권한·소유권·타임스탬프를 그대로 유지합니다.

#### 1-2. 새 버전 다운로드

공식 다운로드 페이지에서 새 패치 버전을 받습니다.

* 다운로드 사이트: https://www.cubrid.org/downloads
* WSL 안에서 직접 받으려면 (예시):
```command
wget https://ftp.cubrid.org/CUBRID_Engine/<version>/Linux/CUBRID-<version>-linux.x86_64.sh
chmod +x CUBRID-<version>-linux.x86_64.sh
```

#### 1-3. 패치 버전 설치

받은 설치 스크립트를 실행하여 같은 위치(`$CUBRID`) 에 덮어쓰기 합니다.

```command
./CUBRID-<version>-linux.x86_64.sh
```

설치가 끝나면 `cubrid_rel` 로 새 버전이 적용되었는지 확인합니다.

```command
cubrid_rel
```

#### 1-4. 백업한 설정 파일 복원

```command
cp -a ~/cubrid_backup/conf/.       $CUBRID/conf/
cp    ~/cubrid_backup/databases.txt $CUBRID_DATABASES/
```

> **참고**: 새 버전이 추가한 기본 설정 파일은 백업본의 동일 이름 파일로 덮어쓰여집니다. 새 버전에서 추가된 신규 파라미터가 있는지 확인하려면, 복원 전에 `diff -r $CUBRID/conf ~/cubrid_backup/conf` 로 차이를 점검하시기를 권장합니다.

#### 1-5. 서비스 시작 및 동작 확인

```command
cubrid service start
cubrid service status
```

서비스가 정상적으로 시작되면 패치 업그레이드가 완료된 것입니다.

---

### 2. 마이너 / 메이저 버전 마이그레이션 (DB 볼륨 비호환)

서로 다른 주 버전 사이에서는 DB 볼륨 형식이 호환되지 않을 수 있습니다. 이런 경우에는 `cubrid unloaddb` 와 `cubrid loaddb` 유틸리티를 이용하여 데이터를 텍스트 파일로 내보낸 뒤, 신규 버전의 CUBRID 에서 다시 가져와야 합니다.

아래 절차는 CUBRID 공식 매뉴얼의 **데이터베이스 마이그레이션 → 권장 시나리오 및 절차** 를 참고하세요.
https://www.cubrid.org/manuals

> 모든 명령은 WSL 안의 `cubrid` 계정에서 실행합니다.
> ```command
> wsl -d CUBRID_For_WSL -u cubrid
> ```

### 권장 시나리오 및 절차

기존 버전의 CUBRID가 운영 중인 상태에서 적용할 수 있는 마이그레이션 시나리오를 설명합니다. 데이터베이스 마이그레이션을 위해서는 `cubrid unloaddb`와 `cubrid loaddb` 유틸리티를 사용합니다. 자세한 내용은 `unloaddb` 및 `loaddb` 를 참조합니다.

#### 2-1 기존 CUBRID 서비스 종료

`cubrid service stop`을 실행하여 기존 CUBRID로 운영되는 모든 서비스 프로세스를 종료한 후, CUBRID 관련 프로세스들이 모두 정상 종료되었는지 확인합니다.

CUBRID 관련 프로세스들이 모두 정상 종료되었는지 확인하려면, Linux에서는 `ps -ef|grep` `cub_`를 실행합니다. `cub_`로 시작하는 프로세스가 없으면 정상적으로 종료된 것이다. Windows에서는 `<Ctrl + Alt + Delete>` 키를 누른 후 [작업 관리자 시작]을 선택합니다. [프로세스] 탭에 `cub_`로 시작하는 프로세스가 없으면 정상적으로 종료된 것이다. CUBRID 서비스 종료 후에도 관련 프로세스가 존재하면 Linux에서는 `kill` 명령으로 강제 종료한 후 `ipcs -m` 명령으로 CUBRID 브로커가 사용 중이던 공유 메모리를 확인하고 삭제합니다. Windows에서는 작업 관리자의 [프로세스] 탭에서 해당 이미지 이름을 마우스 오른쪽 버튼으로 클릭하고 [프로세스 끝내기]를 선택하여 강제 종료합니다.

#### 2-2 기존 데이터베이스 백업

`cubrid backupdb` 유틸리티를 이용하여 기존 버전의 데이터베이스 백업을 수행합니다. 그 이유는 데이터베이스 언로드/로드 작업 중 발생 가능한 장애에 대비하기 위함이다. 데이터베이스 백업에 관한 자세한 내용은 `backupdb`를 참조합니다.

#### 2-3 기존 데이터베이스 언로드

`cubrid unloaddb` 유틸리티를 이용하여 기존 버전의 CUBRID에서 생성된 데이터베이스를 언로드합니다. 데이터베이스 언로드에 관한 자세한 내용은 `unloaddb` 를 참조합니다.

#### 2-4 기존 CUBRID의 환경 설정 파일 보관

`CUBRID/conf` 디렉터리 아래의 `cubrid.conf`, `cubrid_broker.conf`, `cm.conf` 등의 환경 설정 파일을 보관합니다. 이는 기존 CUBRID 데이터베이스 환경에 적용된 파라미터 설정값을 신규 CUBRID 데이터베이스 환경에서 편리하게 적용할 수 있기 때문이다.

#### 2-5 신규 버전의 CUBRID 설치

기존 버전의 CUBRID에서 생성된 데이터의 백업 및 언로드 작업이 완료되었으므로, 기존 버전의 CUBRID 및 데이터베이스를 삭제하고 신규 버전의 CUBRID를 설치합니다. CUBRID 설치에 대한 자세한 내용은 시작하기를 참조합니다.

#### 2-6 신규 CUBRID의 환경 설정

기존 CUBRID의 환경 설정 파일 보관하기 에서 보관한 기존 데이터베이스의 환경 설정 파일을 참고하여 신규 버전의 CUBRID 환경을 설정할 수 있다. 환경 설정에 대한 자세한 내용은 “CUBRID 시작”의 설치와 실행을 참조합니다.

#### 2-7 신규 데이터베이스 로드

`cubrid createdb` 유틸리티를 이용하여 데이터베이스를 생성하고, `cubrid loaddb` 유틸리티를 이용하여 언로드한 데이터를 해당 데이터베이스에 로드합니다. 데이터베이스 생성에 대한 자세한 내용은 “관리자 안내서”의 `createdb` 을 참조하고, 데이터베이스 로드에 대한 자세한 내용은 `loaddb`를 참조합니다.

#### 2-8 신규 데이터베이스 백업

신규 데이터베이스에 데이터 로딩이 완료되면, `cubrid backupdb` 유틸리티를 이용하여 신규 버전의 CUBRID 환경에서 생성된 데이터베이스를 백업합니다. 그 이유는 기존 버전의 CUBRID 환경에서 백업한 데이터를 신규 버전의 CUBRID 환경에서 복구할 수 없기 때문이다. 데이터베이스 백업에 대한 자세한 내용은 `backupdb`를 참고합니다.

### **경고**

* 같은 버전이라 하더라도 백업 및 복구 시 32 비트 데이터베이스 볼륨과 64 비트 데이터베이스 볼륨 간에는 상호 호환을 보장하지 않습니다. 따라서 32 비트 CUBRID 에서 백업한 데이터베이스를 64 비트 CUBRID 에서 복구하거나, 이와 반대로 작업하는 것을 권장하지 않습니다.
* CUBRID 11 버전에서 **TDE** 기능을 사용할 경우 하위 호환성을 제공하지 않으므로, 언로드된 파일을 더 낮은 버전에서 로드할 수 없습니다.

#### 더 자세한 절차

* 공식 매뉴얼 : https://www.cubrid.org/manuals
* `cubrid unloaddb` 옵션 전체: 위 매뉴얼 페이지의 **unloaddb** 항목 참조
* `cubrid loaddb` 옵션 전체: 위 매뉴얼 페이지의 **loaddb** 항목 참조
