---
layout: page
title: 챗뷰 개발자
description: 챗뷰 개발자
---

<script setup>
import {
  VPTeamPage,
  VPTeamMembers
} from 'vitepress/theme'

const developer = [
  {
    avatar: 'https://avatars.githubusercontent.com/u/110483588?v=4',
    name: '안동민',
    title: '개발자',
    desc: '챗뷰 확장 개발 및 유지보수',
    links: [
      { icon: 'github', link: 'https://github.com/andongmin94' },
    ]
  }
]
</script>

<VPTeamPage>
  <VPTeamMembers :members="developer" />
</VPTeamPage>