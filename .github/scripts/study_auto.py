import os
import requests
from openai import OpenAI

OPENAI_API_KEY = os.environ["OPENAI_API_KEY"]
GH_TOKEN = os.environ["GH_TOKEN"]
REPO_FULL = os.environ["REPO_FULL"]
ISSUE_NUMBER = int(os.environ["ISSUE_NUMBER"])

OWNER, REPO = REPO_FULL.split("/")
GH_API_REST = "https://api.github.com"

client = OpenAI(api_key=OPENAI_API_KEY)

issue_resp = requests.get(
    f"{GH_API_REST}/repos/{OWNER}/{REPO}/issues/{ISSUE_NUMBER}",
    headers={"Authorization": f"Bearer {GH_TOKEN}"}
)
issue = issue_resp.json()
title = issue["title"]
body = issue.get("body") or ""

marker = "<!-- GPT_STUDY -->"
notes = body.split(marker, 1)[1].strip() if marker in body else body.strip()

prompt = f"""
너는 KAIST Pintos Project3 Virtual Memory를 공부하는 학생을 위한 '스터디 카드' 설명을 작성한다.

[제목]
{title}

[메모]
{notes}

다음 형식의 마크다운으로 작성해라:

## 개요
- 핵심 요약

## 구현 시 주의사항
- 코드/흐름에서 위험한 포인트

## 공식 문서에서 중요한 포인트
- 꼭 알아야 할 VM 개념

## 토론/질문 거리
- 스터디에서 논의할만한 질문들
"""

res = client.chat.completions.create(
    model="gpt-4.1-mini",
    messages=[{"role": "user", "content": prompt}],
)

generated = res.choices[0].message.content.strip()

new_body = f"""{generated}

---

### 원본 메모
{notes}
"""

patch_resp = requests.patch(
    f"{GH_API_REST}/repos/{OWNER}/{REPO}/issues/{ISSUE_NUMBER}",
    headers={"Authorization": f"Bearer {GH_TOKEN}",
             "Accept": "application/vnd.github+json"},
    json={"body": new_body}
)
