import os
import requests
from openai import OpenAI, RateLimitError

# 환경 변수
OPENAI_API_KEY = os.environ["OPENAI_API_KEY"]
GH_TOKEN = os.environ["GH_TOKEN"]
REPO_FULL = os.environ["REPO_FULL"]
ISSUE_NUMBER = int(os.environ["ISSUE_NUMBER"])

OWNER, REPO = REPO_FULL.split("/")
GH_API_REST = "https://api.github.com"

headers = {
    "Authorization": f"Bearer {GH_TOKEN}",
    "Accept": "application/vnd.github+json",
}

client = OpenAI(api_key=OPENAI_API_KEY)

# 1. 이슈 정보 가져오기
issue_resp = requests.get(
    f"{GH_API_REST}/repos/{OWNER}/{REPO}/issues/{ISSUE_NUMBER}",
    headers=headers,
)
issue_resp.raise_for_status()
issue = issue_resp.json()
title = issue["title"]
body = issue.get("body") or ""

print(f"[study_auto] Issue #{ISSUE_NUMBER} title: {title}")

# 2. 원본 텍스트 추출
# - 기본: 이슈 body 전체를 원본으로 사용
# - 옵션: body 안에 <!-- GPT_STUDY -->가 있으면 그 아래만 사용
marker = "<!-- GPT_STUDY -->"
if marker in body:
    notes = body.split(marker, 1)[1].strip()
else:
    notes = body.strip()

print(f"[study_auto] Notes length: {len(notes)}")

# notes가 비어 있으면 굳이 GPT 호출하지 않고 안내만 남김
if not notes:
    print("[study_auto] Notes is empty, skipping GPT call")
    empty_body = (
        "### 자동 정리 실패\n\n"
        "- 이 이슈의 본문이 비어 있어서 정리할 내용이 없다.\n"
        "- 문서나 메모를 붙여 넣은 뒤 라벨 `study-auto` 를 다시 달아라.\n"
    )
    patch_resp = requests.patch(
        f"{GH_API_REST}/repos/{OWNER}/{REPO}/issues/{ISSUE_NUMBER}",
        headers=headers,
        json={"body": empty_body},
    )
    patch_resp.raise_for_status()
    raise SystemExit(0)

# 3. GPT 프롬프트 구성
prompt = f"""
너에게 아래 원본 문서가 주어진다. (git 문서, 공식 문서, 메모, 코드 설명 등 어떤 것이든 올 수 있다.)
이 문서를 읽고, Pintos / 가상메모리 / 시스템 프로그래밍 공부에 도움이 되도록
GitHub Issue에서 잘 보이도록 **Markdown 형식으로** 재작성해라.

아래 요구사항을 반드시 지켜라:

1. 섹션 구조
   각 섹션은 아래 순서와 제목을 그대로 사용해라.
   제목 앞에 이모지를 붙여 가독성을 높여라.

   ## 🔵 Summary (핵심 요약)
   ## 🟢 Key Points (중요한 개념)
   ## 🟠 Implementation Notes (구현/적용 시 주의사항)
   ## 🟣 Deep Understanding (깊이 이해해야 하는 부분)
   ## ⚪ Questions / Further Study (토론/질문거리)

2. 섹션 사이 구분
   - 각 섹션 사이에는 `---` 한 줄을 넣어서 시각적으로 구분해라.

3. 리스트/강조 스타일
   - 핵심 문장은 항상 bullet(`-`)로 정리해라.
   - 특히 중요한 단어/구문은 **굵게** 처리해라.
   - 필요한 경우 `*기울임*`도 사용해도 된다.

4. 코드 블록 자동 감지
   - 원본 문서에 C 코드, 쉘 커맨드, 설정 파일 등이 섞여 있는 경우,
     "코드처럼 보이는 줄"은 모두 fenced code block 으로 감싸라.
   - 예:
     - C/시스템 코드: ```c ... ```
     - 쉘/터미널 명령: ```sh ... ```
     - 그 밖에 애매하면 ```text ... ``` 를 사용해라.
   - 예시 패턴:
     - 세미콜론(;)이 자주 나오거나, `{` `}` `#include` `struct` 같은 키워드가 있으면 C 코드로 간주.
     - `git `, `make `, `./pintos`, `./test-...` 등으로 시작하면 쉘 명령으로 간주.
   - 코드 블록 안에서는 들여쓰기/줄바꿈을 최대한 원본 그대로 유지해라.

5. 문서 톤
   - 한국어로 작성하되, **기술 용어와 식별자(함수명, 변수명, 구조체명)는 영어 그대로** 사용해라.
   - 불필요한 서론, 잡담, 중복 설명은 모두 제거하고, "나중에 다시 보기 좋은 기술 노트" 느낌으로 간결하게 작성해라.

아래는 원본 문서이다:

[원본 문서 시작]
{notes}
[원본 문서 끝]
"""

try:
    res = client.chat.completions.create(
        model="gpt-4.1-mini",
        messages=[{"role": "user", "content": prompt}],
        max_tokens=800,  # 응답 길이 상한 (비용 제한용)
    )
    generated = res.choices[0].message.content.strip()
    print("[study_auto] GPT call success")

    # 4. 이슈 본문을 GPT가 만든 정리 내용으로 완전히 교체
    new_body = generated

except RateLimitError:
    print("[study_auto] Rate limit / quota error, writing fallback message")
    new_body = (
        "### 자동 정리 실패\n\n"
        "- OpenAI API 한도(quota)가 초과되어 이 이슈에 대한 자동 정리를 수행하지 못했다.\n"
        "- Billing/Usage를 확인한 후 `study-auto` 라벨을 다시 달면 재실행할 수 있다.\n"
    )

# 5. 이슈 본문 업데이트
patch_resp = requests.patch(
    f"{GH_API_REST}/repos/{OWNER}/{REPO}/issues/{ISSUE_NUMBER}",
    headers=headers,
    json={"body": new_body},
)
patch_resp.raise_for_status()
print(f"[study_auto] Issue body updated: status={patch_resp.status_code}")
