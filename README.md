# webbench
webbench support the post from file ,so as the head

the -A xxx support the assertion
the -h xxx support setting the head of request

example:
./webbench  -c 2 -t 10 -F "post.txt" -h "accept:*" -A "success" -H "head.txt" http://t17.aaa.com

post.txt:

coupon_code=testyou11

coupon_code=testyou12

coupon_code=testyou13

head.txt:

token:111111

token:222222
