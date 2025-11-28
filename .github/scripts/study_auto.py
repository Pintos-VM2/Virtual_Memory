import os
import requests
from openai import OpenAI, RateLimitError

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

# 2. 원본 메모 추출
marker = "<!-- GPT_STUDY -->"
if marker in body:
    notes = body.split(marker, 1)[1].strip()
else:
    notes = body.strip()

print(f"[study_auto] Notes length: {len(notes)}")

prompt = f"""
너는 KAIST Pintos Project3 Virtual Memory를 공부하는 학생을 위한 '스터디 카드' 설명을 작성한다.

[제목]
{title}

[메모]
{notes}

다음 형식의 마크다운으로 작성해라:

## 개요
- 핵심 요약 1~3줄

## 구현 시 주의사항
- 코드/흐름에서 위험한 포인트

## 공식 문서에서 중요한 포인트
- 꼭 알아야 할 VM 개념

## 토론/질문 거리
- 스터디에서 논의할만한 질문들
"""

try:
    res = client.chat.completions.create(
        model="gpt-4.1-mini",
        messages=[{"role": "user", "content": prompt}],
        max_tokens=800,
    )
    generated = res.choices[0].message.content.strip()
    print("[study_auto] GPT call success")

    new_body = f"""{generated}

---

### 원본 메모
{notes}
"""

except RateLimitError:
    print("[study_auto] Rate limit / quota error, writing fallback message")
    new_body = f"""### 자동 정리 실패

- OpenAI API 한도(quota)가 초과되어 이 이슈에 대한 자동 정리를 수행하지 못했다.
- Billing/Usage를 확인한 후 `study-auto` 라벨을 다시 달면 재실행할 수 있다.

---

### 원본 메모
{notes}
"""

# 3. 이슈 본문 업데이트
patch_resp = requests.patch(
    f"{GH_API_REST}/repos/{OWNER}/{REPO}/issues/{ISSUE_NUMBER}",
    headers=headers,
    json={"body": new_body},
)
patch_resp.raise_for_status()
print(f"[study_auto] Issue body updated: status={patch_resp.status_code}")

# 4. 댓글 하나 남겨서 확인하기
comment_resp = requests.post(
    f"{GH_API_REST}/repos/{OWNER}/{REPO}/issues/{ISSUE_NUMBER}/comments",
    headers=headers,
    json={"body": "✅ study-auto workflow가 이 이슈를 처리했습니다."},
)
comment_resp.raise_for_status()
print(f"[study_auto] Comment created: status={comment_resp.status_code}")
