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
너에게 아래에 붙여넣은 원본 문서가 주어진다. (git 문서, 공식 문서, 메모, 코드 설명 등 어떤 것이든 올 수 있다.)
이 문서를 읽고, Pintos / 가상메모리 / 시스템 프로그래밍 공부에 도움이 되도록 아래 형식으로 정리해라.

형식은 반드시 아래 마크다운 구조를 따른다. 불필요한 수다는 빼고, 핵심적인 내용만 적어라.

## Summary (핵심 요약)
- 이 문서의 주요 내용을 3~5줄로 요약해라.

## Key Points (중요한 개념)
- 개념, 정의, 중요한 규칙을 bullet로 정리해라.

## Implementation Notes (구현/적용 시 주의사항)
- 실제 코드 작성/수정/디버깅 시 주의해야 할 점을 bullet로 정리해라.
- 함수/파일 이름, 제어 흐름, 데이터 구조 관점에서 구체적으로 써라.

## Deep Understanding (깊이 이해해야 하는 부분)
- 이 문서에서 깊게 이해해야 하는 개념, 내부 동작, 흔한 오해를 bullet로 정리해라.

## Questions / Further Study (토론/질문거리)
- 스스로 더 공부하거나, 스터디에서 토론해 볼 만한 질문들을 bullet로 나열해라.

원본이 영어여도 결과는 한국어로 작성하되, 기술 용어/식별자는 영어 그대로 사용해도 된다.

아래는 원본 문서이다.

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
